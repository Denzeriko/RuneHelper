#pragma once

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>

#include <string>
#include <vector>

#include "core/Config.h"

struct LootLine
{
    std::string text;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
};

struct OcrRowDebug
{
    cv::Rect row;
    int psm = 0;
    std::string rawText;
    std::string text;
    float confidence = 0.0f;
};

class OCR
{
public:
    OCR() = default;
    ~OCR();

    OCR(const OCR&) = delete;
    OCR& operator=(const OCR&) = delete;

    bool Init(const std::string& tessdataPath);
    void Reset();

    void SetConfig(const AppConfig* config);
    void SetDebug(bool enabled);

    std::vector<LootLine> RecognizeLoot(const cv::Mat& src);
    std::vector<OcrRowDebug> RecognizeRows(const cv::Mat& src, const std::vector<int>& psms, const std::string& debugPrefix = {});
    cv::Mat PreprocessForDebug(const cv::Mat& src);
    std::vector<cv::Rect> DetectRows(const cv::Mat& prepared) const;

private:
    tesseract::TessBaseAPI api_;
    bool initialized_ = false;
    bool debug_ = false;
    int appliedPsm_ = -1;

    const AppConfig* config_ = nullptr;

private:
    void SetupTesseract();
    void ApplyRuntimeConfig(bool force = false);
    cv::Mat Preprocess(const cv::Mat& src);
    std::vector<LootLine> RecognizePrepared(const cv::Mat& img);
    std::string RecognizeRowText(const cv::Mat& img, int psm, float* confidence);
    void DebugTesseract();
    static void Trim(std::string& s);
    static std::string PostProcessText(std::string text);
};
