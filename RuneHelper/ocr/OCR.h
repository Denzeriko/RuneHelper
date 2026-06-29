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
    float conf = 0.0f;
};

class OCR
{
public:
    OCR() = default;
    ~OCR();

    OCR(const OCR&) = delete;
    OCR& operator=(const OCR&) = delete;

    bool Init(const std::string& tessdataPath);

    void SetConfig(const AppConfig* config);
    void SetDebug(bool enabled);

    std::vector<LootLine> RecognizeLoot(const cv::Mat& src);

private:
    tesseract::TessBaseAPI api_;
    bool initialized_ = false;
    bool debug_ = false;

    const AppConfig* config_ = nullptr;

private:
    void SetupTesseract();
    std::vector<double> BuildThresholds() const;
    cv::Mat Preprocess(const cv::Mat& src);
    cv::Mat Preprocess(const cv::Mat& src, double thresholdValue);
    std::vector<LootLine> RecognizePrepared(const cv::Mat& img);
    void DebugTesseract();
    static void Trim(std::string& s);
};