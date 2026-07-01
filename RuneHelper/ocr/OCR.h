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
    void SetupTesseractApi(tesseract::TessBaseAPI& api);
    bool ReinitializeWorkers(const AppConfig& config);

    void SetConfig(const AppConfig* config);

    std::vector<LootLine> RecognizePreparedWithApi(tesseract::TessBaseAPI& api, const cv::Mat& img);
    std::vector<LootLine> RecognizeLoot(const cv::Mat& src, const AppConfig& config);

private:
    tesseract::TessBaseAPI api_;
    bool initialized_ = false;

    const AppConfig* config_ = nullptr;

    std::mutex workerMutex_;
    std::string tessdataPath_;
    std::vector<std::unique_ptr<tesseract::TessBaseAPI>> workerApis_;

private:
    static std::vector<double> BuildThresholds(const AppConfig& config);
    static void Trim(std::string& s);
};
