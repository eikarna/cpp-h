#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include <signal.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

std::mutex file_mutex;

std::string read_file_content(const std::string &path) {
    std::lock_guard<std::mutex> lock(file_mutex);

    // Open the file
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cerr << "Failed to open file: " << strerror(errno) << std::endl;
        return "";
    }

    // Get file size
    struct stat st;
    if (fstat(fd, &st) == -1) {
        std::cerr << "Failed to get file size: " << strerror(errno) << std::endl;
        close(fd);
        return "";
    }
    size_t file_size = st.st_size;

    // Memory-map the file
    char *mapped = static_cast<char *>(mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (mapped == MAP_FAILED) {
        std::cerr << "Failed to memory-map file: " << strerror(errno) << std::endl;
        close(fd);
        return "";
    }

    // Copy file content to a string
    std::string content(mapped, file_size);

    // Unmap and close file
    munmap(mapped, file_size);
    close(fd);

    return content;
}

// Define handler functions
void default_handler(const httplib::Request &req, httplib::Response &res) {
    res.set_content("Simple C++ HTTP/s Server for Growtopia Private Server!\n©️ Eikarna", "text/plain");
}

void server_data_handler(const httplib::Request &req, httplib::Response &res) {
    // Handle "/growtopia/server_data.php" path here
    std::string content = read_file_content("server_data.txt");
    if (content.empty()) {
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
    } else {
        res.set_content(content, "text/plain");
    }
}

int main() {
    using namespace httplib;
    // Ignore SIGPIPE signal
    signal(SIGPIPE, SIG_IGN);

    // Create server instance with SSL support
    SSLServer svr("./certs/growtopia.pem", "./certs/growtopia.key.pem");

    // Check if the server is valid
    if (!svr.is_valid()) {
        std::cerr << "Server has an error: " << strerror(errno) << std::endl;
        return -1;
    }

    // Set handler for default path "/"
    svr.Get("/", default_handler);

    // Set handler for "/growtopia/server_data.php"
    svr.Post("/growtopia/server_data.php", server_data_handler);

    // Set handler for "/cache" from folder "./cache"
    auto ret = svr.set_mount_point("/cache", "./cache");
    if (!ret) {
        std::cerr << strerror(errno) << std::endl;
        return -1;
    }

    // Enable optimization for preventing DoS/DDoS attack
    svr.set_read_timeout(1, 0); // 1 seconds
    svr.set_keep_alive_max_count(1); // Limit maximum keep-alive connections
    svr.set_payload_max_length(1 * 1024 * 1024); // Set maximum payload length to 2 MB

    // Start server on port 443 (HTTPS default)
    std::cout << "Starting server on port 443" << std::endl;
    svr.listen("0.0.0.0", 443);

    return 0;
}
