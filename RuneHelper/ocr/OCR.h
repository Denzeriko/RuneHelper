#pragma once

#include <opencv2/core.hpp>
#include <tesseract/baseapi.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/Config.h"

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

class OcrRowCache;

class OCR
{
public:
    OCR() = default;
    ~OCR() = default;

    OCR(const OCR&) = delete;
    OCR& operator=(const OCR&) = delete;

    bool Init(std::string_view traineddata);
    void SetupTesseractApi(tesseract::TessBaseAPI& api);

    std::vector<LootLine> RecognizeLoot(const cv::Mat& source, const AppConfig& config, OcrRowCache* rowCache = nullptr);
    std::vector<cv::Rect> FindLootRows(const cv::Mat& gray) const;
    std::vector<LootLine> RecognizeTextOnly(
        tesseract::TessBaseAPI& api,
        const cv::Mat& textGray,
        const std::filesystem::path& debugBinPath = {}
    );

private:
    void ReportPreparation(bool scaled, bool normalized, int sourceWidth, int readWidth, double p50, double p95);

    bool initialized_ = false;
    bool readScaled_ = false;
    bool readNormalized_ = false;

    std::vector<std::unique_ptr<tesseract::TessBaseAPI>> apis_;
    std::mutex apiMutex_;

    static void Trim(std::string& s);
};
