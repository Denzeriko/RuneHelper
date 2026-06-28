#include "OCR.h"

#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <filesystem>

OCR::~OCR()
{
    if (initialized_)
        api_.End();
}

bool OCR::Init(const std::string& tessdataPath)
{
    LOG_INFO("OCR::Init tessdataPath = " + tessdataPath);

    std::filesystem::path engPath = std::filesystem::path(tessdataPath) / "eng.traineddata";

    if (!std::filesystem::exists(engPath))
    {
        LOG_ERROR("eng.traineddata not found: " + engPath.string());
        return false;
    }

    LOG_INFO("eng.traineddata found: " + engPath.string());
    LOG_INFO("eng.traineddata size: " + std::to_string(std::filesystem::file_size(engPath)));

    int rc = api_.Init(tessdataPath.c_str(), "eng");

    if (rc != 0)
    {
        LOG_ERROR("Tesseract api.Init failed, rc=" + std::to_string(rc));
        return false;
    }

    initialized_ = true;
    LOG_INFO("OCR initialized");

    SetupTesseract();
    return true;
}

void OCR::SetConfig(const AppConfig* config)
{
    config_ = config;
}

void OCR::SetDebug(bool enabled)
{
    debug_ = enabled;
}

void OCR::SetupTesseract()
{
    api_.SetPageSegMode(tesseract::PSM_SPARSE_TEXT);
    api_.SetVariable("debug_file", "NUL");
    api_.SetVariable("classify_bln_numeric_mode", "0");
    api_.SetVariable("preserve_interword_spaces", "1");
    api_.SetVariable(
        "tessedit_char_whitelist",
        "0123456789xABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz() -'"
    );

    LOG_INFO("SetupTesseract done");
}

std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& src)
{
    if (!initialized_ || src.empty())
        return {};

    cv::Mat prepared = Preprocess(src);

    return RecognizePrepared(prepared);
}

cv::Mat OCR::Preprocess(const cv::Mat& src)
{
    const double scale = config_ ? config_->ocrScale : 1.0;
    const double thresholdValue = config_ ? config_->ocrThreshold : 130.0;

    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, {}, scale, scale, cv::INTER_CUBIC );

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::erode(gray, gray, kernel);
    cv::threshold(gray, gray, thresholdValue, 255, cv::THRESH_BINARY);

    if (debug_ || (config_ && config_->debugOCR))
        cv::imwrite("ocr_debug.png", gray);

    return gray;
}

std::vector<LootLine> OCR::RecognizePrepared(const cv::Mat& img)
{
    api_.SetImage(img.data, img.cols, img.rows, 1, static_cast<int>(img.step));

    api_.Recognize(nullptr);

    if (debug_ || (config_ && config_->debugOCR))
        DebugTesseract();

    std::vector<LootLine> result;

    tesseract::ResultIterator* ri = api_.GetIterator();
    if (!ri)
        return result;

    do
    {
        char* text =
            ri->GetUTF8Text(tesseract::RIL_TEXTLINE);

        if (!text)
            continue;

        std::string line(text);
        delete[] text;

        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

        Trim(line);

        if (line.empty())
            continue;

        float conf = ri->Confidence(tesseract::RIL_TEXTLINE);

        if (conf < 35.0f)
            continue;

        int x1, y1, x2, y2;

        ri->BoundingBox(tesseract::RIL_TEXTLINE, &x1, &y1, &x2, &y2);

        result.push_back({line, x1, y1, x2, y2});

    } while (ri->Next(tesseract::RIL_TEXTLINE));

    return result;
}

void OCR::DebugTesseract()
{
    auto* ri = api_.GetIterator();

    if (!ri)
    {
        std::cout << "No iterator\n";
        return;
    }

    do
    {
        int x1, y1, x2, y2;

        ri->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2);

        float conf = ri->Confidence(tesseract::RIL_WORD);

        char* word = ri->GetUTF8Text(tesseract::RIL_WORD);

        if (word)
        {
            std::cout
                << "WORD: [" << word << "] "
                << "conf=" << conf << " "
                << "box=(" << x1 << "," << y1
                << "," << x2 << "," << y2 << ")"
                << std::endl;

            delete[] word;
        }

    } while (ri->Next(tesseract::RIL_WORD));
}

//yuck
void OCR::Trim(std::string& s)
{
    s.erase(
        s.begin(),
        std::find_if(
            s.begin(),
            s.end(),
            [](unsigned char c)
            {
                return !std::isspace(c);
            }));

    s.erase(
        std::find_if(
            s.rbegin(),
            s.rend(),
            [](unsigned char c)
            {
                return !std::isspace(c);
            }).base(),
                s.end());
}