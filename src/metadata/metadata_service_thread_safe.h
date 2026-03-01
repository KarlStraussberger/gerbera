// thread-safe_jpeg_metadata_extraction.h

#ifndef THREAD_SAFE_JPEG_METADATA_EXTRACTION_H
#define THREAD_SAFE_JPEG_METADATA_EXTRACTION_H

#include <mutex>
#include <jpeglib.h>
#include <string>

class ThreadSafeJPEGMetadataExtractor {
public:
    ThreadSafeJPEGMetadataExtractor();
    ~ThreadSafeJPEGMetadataExtractor();

    bool extractMetadata(const std::string &filePath);

private:
    std::mutex mtx;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    bool readMetadata(const std::string &filePath);
};

#endif // THREAD_SAFE_JPEG_METADATA_EXTRACTION_H