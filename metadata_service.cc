#include <iostream>
#include <string>
#include <mutex>

class JpegExtractor {
public:
    // Method to extract JPEG resolution
    static std::pair<int, int> extractJpegResolution(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Implementation of JPEG resolution extraction
        // For demonstration, returning dummy values
        return {1920, 1080};
    }

    // Method to extract JPEG metadata
    static std::string extractJpegMetadata(const std::string& filePath) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Implementation of JPEG metadata extraction
        // For demonstration, returning dummy metadata
        return "JPEG Metadata";
    }

private:
    static std::mutex mutex_; // Mutex for thread safety
};

std::mutex JpegExtractor::mutex_; // Definition of the mutex

int main() {
    std::string filePath = "path/to/jpeg/file.jpg";
    auto resolution = JpegExtractor::extractJpegResolution(filePath);
    std::string metadata = JpegExtractor::extractJpegMetadata(filePath);
    std::cout << "Resolution: " << resolution.first << "x" << resolution.second << '\n';
    std::cout << "Metadata: " << metadata << '\n';
    return 0;
}