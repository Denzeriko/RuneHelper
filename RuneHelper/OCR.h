#pragma once

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>

#include <string>
#include <vector>

struct LootLine
{
    std::string text;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
};

class OCR
{
public:
    OCR() = default;
    ~OCR();

    OCR(const OCR&) = delete;
    OCR& operator=(const OCR&) = delete;

    bool Init(const std::string& tessdataPath);

    std::vector<LootLine> RecognizeLoot(const cv::Mat& src);

    void SetDebug(bool enabled);

private:
    tesseract::TessBaseAPI api_;
    bool initialized_ = false;
    bool debug_ = false;

private:
    cv::Mat Preprocess(const cv::Mat& src);
    std::vector<LootLine> RecognizePrepared(const cv::Mat& img);

    void SetupTesseract();
    void DebugTesseract();
    static void Trim(std::string& s);
};
