#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/Config.h"
#include "ocr/LineReader.h"

struct LootLine
{
    std::string text;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    float conf = 0.0f;
};

struct TextLevels
{
    double p25 = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
};

double ReadingScale(int width);
cv::Mat NormalizeTextLevels(const cv::Mat& gray, const TextLevels& levels);

class OcrRowCache;

class OCR
{
public:
    OCR() = default;
    ~OCR() = default;

    OCR(const OCR&) = delete;
    OCR& operator=(const OCR&) = delete;

    bool Init(std::string_view model);

    std::vector<LootLine> RecognizeLoot(const cv::Mat& source, const AppConfig& config, OcrRowCache* rowCache = nullptr);
    std::vector<cv::Rect> FindLootRows(const cv::Mat& gray) const;
    std::vector<LootLine> RecognizeTextOnly(const cv::Mat& textGray, const std::filesystem::path& debugPath = {}) const;

private:
    void ReportPreparation(bool scaled, bool normalized, int sourceWidth, int readWidth, double p50, double p95);
    void ReportPanel(const cv::Rect& panel, const cv::Size& source);

    bool initialized_ = false;
    bool readScaled_ = false;
    bool readNormalized_ = false;
    bool readTrimmed_ = false;

    LineReader reader_;
    std::size_t workers_ = 1;
    std::mutex mutex_;

    static void Trim(std::string& s);
};
