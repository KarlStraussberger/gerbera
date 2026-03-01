// file_lock_manager.h

#ifndef FILE_LOCK_MANAGER_H
#define FILE_LOCK_MANAGER_H

#include <mutex>
#include <unordered_map>
#include <string>

class FileLockManager {
public:
    // Acquire a lock for a specific file
    void lockFile(const std::string& filePath) {
        std::unique_lock<std::mutex> lock(mutex_);
        fileLocks_[filePath].lock();
    }

    // Release a lock for a specific file
    void unlockFile(const std::string& filePath) {
        std::unique_lock<std::mutex> lock(mutex_);
        fileLocks_[filePath].unlock();
    }

    // Check if a file is locked
    bool isFileLocked(const std::string& filePath) {
        std::unique_lock<std::mutex> lock(mutex_);
        return fileLocks_[filePath].try_lock();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::mutex> fileLocks_;
};

#endif // FILE_LOCK_MANAGER_H
