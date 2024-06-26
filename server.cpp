#define CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_TCP_NODELAY true
#define CPPHTTPLIB_THREAD_POOL_COUNT 8192
#define CPPHTTPLIB_RECV_BUFSIZ size_t(1024u)
#include "httplib.h"
#include <signal.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <shared_mutex>
#include <thread>

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
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#endif

std::unordered_map<std::string, std::atomic<int>> ip_request_count;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> ip_blocked_until;
std::shared_mutex cache_mutex;
std::string server_data_cache;

const int RATE_LIMIT = 20; // Max requests per IP per second
const int BLOCK_DURATION = 5; // Block duration in seconds

void log_message(const std::string &message) {
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> lock(log_mutex);
    // Print to console
    std::cerr << message << std::endl;
    // Save to server.log
    std::ofstream log_file("server.log", std::ios::app);
    log_file << message << std::endl;
}

std::string read_file_content(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << strerror(errno) << std::endl;
        return "";
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    file.close();

    return contents.str();
}

void preload_server_data() {
    std::lock_guard<std::shared_mutex> lock(cache_mutex);
    server_data_cache = read_file_content("server_data.txt");
}

void default_handler(const httplib::Request &req, httplib::Response &res) {
    res.set_content("Simple C++ HTTP/s Server for Growtopia Private Server!\n©️ Eikarna", "text/plain");
}

void server_data_handler(const httplib::Request &req, httplib::Response &res) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex);
    if (server_data_cache.empty()) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
    } else {
        res.set_content(server_data_cache, "text/plain");
    }
}

void log_rate_limit_block(const std::string &ip) {
    log_message("Blocking IP " + ip + " due to rate limit exceeded.");
}

bool rate_limit_check(const std::string &ip) {
    auto now = std::chrono::steady_clock::now();

    if (ip_blocked_until.find(ip) != ip_blocked_until.end()) {
        if (now < ip_blocked_until[ip]) {
            return false;
        } else {
            ip_blocked_until.erase(ip);
            ip_request_count[ip] = 0;
        }
    }

    ip_request_count[ip]++;

    if (ip_request_count[ip] > RATE_LIMIT) {
        ip_blocked_until[ip] = now + std::chrono::seconds(BLOCK_DURATION);
        ip_request_count[ip] = 0;
        log_rate_limit_block(ip);
        return false;
    }

    return true;
}

void configure_system_limits() {
#ifndef _WIN32
    // Increase file descriptors limit
    struct rlimit limit;
    limit.rlim_cur = 65536;
    limit.rlim_max = 65536;
    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        std::cerr << "Failed to set file descriptor limit: " << strerror(errno) << std::endl;
    }
#endif
}

int main() {
    using namespace httplib;

    preload_server_data();
    configure_system_limits();

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    SSLServer svr("./certs/growtopia.pem", "./certs/growtopia.key.pem");

    if (!svr.is_valid()) {
        std::cerr << "Server has an error: " << strerror(errno) << std::endl;
        return -1;
    }

    // Configure SSL context for performance
    SSL_CTX* ssl_ctx = svr.ssl_context();
    if (ssl_ctx) {
        SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_SERVER);
        SSL_CTX_set_timeout(ssl_ctx, 300); // 5 minutes session timeout
        SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_TICKET); // Disable session tickets for simplicity
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_RELEASE_BUFFERS); // Release memory buffers when not in use
    }

    svr.set_file_request_handler([](const Request &req, Response &res) {
        if (req.path.rfind("/cache", 0) == 0) {
            log_message("IP " + req.remote_addr + " accessing cache at: " + req.path);
        }
    });

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
    svr.set_read_timeout(1, 0); // 1 second read timeout
    svr.set_write_timeout(5, 0); // 5 seconds write timeout
    svr.set_keep_alive_max_count(3); // Allow up to 3 keep-alive requests
    svr.set_payload_max_length(1 * 1024 * 1024); // Set maximum payload length to 1 MB

    std::cout << "Starting server on port 443" << std::endl;
    if (!svr.listen("0.0.0.0", 443)) {
        std::cerr << "Failed to bind to port 443" << std::endl;
        return -1;
    }

    return 0;
}
