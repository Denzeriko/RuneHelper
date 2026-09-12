#pragma once

#include <opencv2/core.hpp>
#include <tesseract/baseapi.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/Config.h"
#include "ocr/RunePatternMatcher.h"

struct LootLine
{
    std::string text;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    float conf = 0.0f;
};

class OCR
{
public:
    OCR() = default;
    ~OCR() = default;

    OCR(const OCR&) = delete;
    OCR& operator=(const OCR&) = delete;

    bool Init(const std::string& tessdataPath);
    void SetupTesseractApi(tesseract::TessBaseAPI& api);

    std::vector<LootLine> RecognizeLoot(
        const cv::Mat& bgr,
        const cv::Mat& gray,
        const AppConfig& config,
        const std::vector<RunePatternMatch>& runeMatches = {});
    std::vector<cv::Rect> FindLootRows(const cv::Mat& gray) const;
    std::vector<LootLine> RecognizeTextOnly(tesseract::TessBaseAPI& api, const cv::Mat& textGray, const std::string& debugBinPath = {});

private:
    bool initialized_ = false;

    std::string tessdataPath_;
    std::unique_ptr<tesseract::TessBaseAPI> api_;
    std::mutex apiMutex_;

    static void Trim(std::string& s);
};
