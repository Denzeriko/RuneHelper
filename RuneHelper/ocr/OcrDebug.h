#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

bool SaveOcrDebugImage(const std::filesystem::path& path, const cv::Mat& img);

void SaveOcrDebugText(
    const std::filesystem::path& debugPath,
    const std::string& rawText,
    const std::string& trimmedText,
    float confidence,
    const char* status
);

class OcrDebugDump
{
public:
    bool Start(const cv::Mat& source, const cv::Mat& gray, bool savePrepared);
    std::filesystem::path SaveRow(
        std::size_t index,
        const cv::Mat& rowGray,
        const cv::Mat& textGray,
        const cv::Rect& rowRect,
        int textX
    );
    void Finish() const;

private:
    std::filesystem::path RowPath(std::size_t index, const char* suffix) const;

    std::filesystem::path dir_;
    cv::Mat rows_;
};
