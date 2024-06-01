#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <signal.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define open _open
#define close _close
#define fstat _fstat
#define read _read
#define stat _stat
#define O_RDONLY _O_RDONLY
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

std::mutex file_mutex;
std::mutex rate_limit_mutex;
std::unordered_map<std::string, int> ip_request_count;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> ip_blocked_until;

const int RATE_LIMIT = 100; // Max requests per IP per minute
const int BLOCK_DURATION = 60; // Block duration in seconds

std::string read_file_content(const std::string &path) {
    std::lock_guard<std::mutex> lock(file_mutex);

    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Failed to open file: " << strerror(errno) << std::endl;
        return "";
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        std::cerr << "Failed to get file size: " << strerror(errno) << std::endl;
        close(fd);
        return "";
    }
    size_t file_size = st.st_size;

#ifdef _WIN32
    char* buffer = new char[file_size];
    if (read(fd, buffer, file_size) == -1) {
        std::cerr << "Failed to read file: " << strerror(errno) << std::endl;
        close(fd);
        delete[] buffer;
        return "";
    }
    std::string content(buffer, file_size);
    delete[] buffer;
#else
    char *mapped = static_cast<char *>(mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (mapped == MAP_FAILED) {
        std::cerr << "Failed to memory-map file: " << strerror(errno) << std::endl;
        close(fd);
        return "";
    }

    std::string content(mapped, file_size);
    munmap(mapped, file_size);
#endif

    close(fd);

    return content;
}

void default_handler(const httplib::Request &req, httplib::Response &res) {
    res.set_content("Simple C++ HTTP/s Server for Growtopia Private Server!\n©️ Eikarna", "text/plain");
}

void server_data_handler(const httplib::Request &req, httplib::Response &res) {
    std::string content = read_file_content("server_data.txt");
    if (content.empty()) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
    } else {
        res.set_content(content, "text/plain");
    }
}

void log_rate_limit_block(const std::string &ip) {
    std::cerr << "Blocking IP " << ip << " due to rate limit exceeded." << std::endl;
}

bool rate_limit_check(const std::string &ip) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex);

    auto now = std::chrono::steady_clock::now();

    if (ip_blocked_until.find(ip) != ip_blocked_until.end()) {
        if (now < ip_blocked_until[ip]) {
            return false;
        } else {
            ip_blocked_until.erase(ip);
            ip_request_count.erase(ip);
        }
    }

    ip_request_count[ip]++;

    if (ip_request_count[ip] > RATE_LIMIT) {
        ip_blocked_until[ip] = now + std::chrono::seconds(BLOCK_DURATION);
        ip_request_count.erase(ip);
        log_rate_limit_block(ip);
        return false;
    }

    return true;
}

int main() {
    using namespace httplib;

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    SSLServer svr("./certs/growtopia.pem", "./certs/growtopia.key.pem");

    if (!svr.is_valid()) {
        std::cerr << "Server has an error: " << strerror(errno) << std::endl;
        return -1;
    }

    // Configure SSL context for session resumption
    SSL_CTX* ssl_ctx = svr.ssl_context();
    if (ssl_ctx) {
        SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_SERVER);
        SSL_CTX_set_timeout(ssl_ctx, 300); // 5 minutes session timeout
        SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TICKET); // Disable session tickets for simplicity
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY); // Automatically retry on TLS handshake errors
    }

    svr.Get("/", [](const Request &req, Response &res) {
        std::string client_ip = req.remote_addr;

        if (!rate_limit_check(client_ip)) {
            res.status = 429;
            res.set_content("Too Many Requests", "text/plain");
            return;
        }

        default_handler(req, res);
    });

    svr.Post("/growtopia/server_data.php", [](const Request &req, Response &res) {
        std::string client_ip = req.remote_addr;

        if (!rate_limit_check(client_ip)) {
            res.status = 429;
            res.set_content("Too Many Requests", "text/plain");
            return;
        }

        server_data_handler(req, res);
    });

    auto ret = svr.set_mount_point("/cache", "./cache");
    if (!ret) {
        std::cerr << strerror(errno) << std::endl;
        return -1;
    }

    // Increase timeouts to handle more concurrent connections
    svr.set_read_timeout(1, 0); // 1 seconds read timeout
    svr.set_write_timeout(1, 0); // 1 seconds write timeout
    svr.set_keep_alive_max_count(10); // Allow up to 10 keep-alive requests
    svr.set_payload_max_length(1 * 1024 * 1024); // Set maximum payload length to 1 MB

    const int thread_pool_size = std::thread::hardware_concurrency() * 2; // Increase thread pool size
    std::vector<std::thread> thread_pool;
    std::atomic<bool> stop_flag{false};

    for (int i = 0; i < thread_pool_size; ++i) {
        thread_pool.emplace_back([&svr, &stop_flag]() {
            while (!stop_flag.load()) {
                svr.listen_after_bind();
            }
        });
    }

    std::cout << "Starting server on port 443" << std::endl;
    if (!svr.bind_to_port("0.0.0.0", 443)) {
        std::cerr << "Failed to bind to port 443" << std::endl;
        stop_flag.store(true);
    }

    for (auto &t : thread_pool) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
