#ifndef THREAD_SAFE_IO_HANDLER_H
#define THREAD_SAFE_IO_HANDLER_H

#include <mutex>
#include <fstream>
#include <string>

class ThreadSafeIOHandler {
public:
    ThreadSafeIOHandler(const std::string& filename) : filename(filename) {}

    void write(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex);
        std::ofstream file(filename, std::ios::app);
        file << data;
    }

    std::string read() {
        std::lock_guard<std::mutex> lock(mutex);
        std::ifstream file(filename);
        std::string content;
        std::string line;
        while (std::getline(file, line)) {
            content += line + "\n";
        }
        return content;
    }

private:
    std::string filename;
    std::mutex mutex;
};

#endif // THREAD_SAFE_IO_HANDLER_H
