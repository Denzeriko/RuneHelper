#include "OCR.h"

#include "core/Logger.h"
#include "ocr/NameNormalizer.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <filesystem>
#include <future>

OCR::~OCR()
{
    if (initialized_)
        api_.End();
}

bool OCR::Init(const std::string& tessdataPath)
{
    LOG_INFO("OCR::Init tessdataPath = " + tessdataPath);

    tessdataPath_ = tessdataPath;
    std::filesystem::path engPath = std::filesystem::path(tessdataPath) / "eng.traineddata";

    if (!std::filesystem::exists(engPath))
    {
        LOG_ERROR("eng.traineddata not found: " + engPath.string());
        return false;
    }

    int rc = api_.Init(tessdataPath.c_str(), "eng", tesseract::OEM_LSTM_ONLY);
    if (rc != 0)
    {
        LOG_ERROR("Tesseract api.Init failed, rc=" + std::to_string(rc));
        return false;
    }

    SetupTesseractApi(api_);

    int passes = config_ ? config_->ocrPasses : 1;
    passes = std::clamp(passes, 1, 8);

    workerApis_.clear();
    workerApis_.reserve(passes);

    for (int i = 0; i < passes; ++i)
    {
        auto api = std::make_unique<tesseract::TessBaseAPI>();

        rc = api->Init(tessdataPath.c_str(), "eng", tesseract::OEM_LSTM_ONLY);

        if (rc != 0)
        {
            LOG_ERROR("Tesseract worker api.Init failed, rc=" + std::to_string(rc));
            return false;
        }

        SetupTesseractApi(*api);

        workerApis_.push_back(std::move(api));
    }

    initialized_ = true;
    LOG_INFO("OCR initialized");

    return true;
}

void OCR::SetupTesseractApi(tesseract::TessBaseAPI& api)
{
    api.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);

    api.SetVariable(
        "tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        " '-"
    );

    api.SetVariable("preserve_interword_spaces", "1");
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

bool OCR::ReinitializeWorkers()
{
    std::lock_guard lock(workerMutex_);

    workerApis_.clear();

    int passes = std::clamp(config_->ocrPasses, 1, 6);

    workerApis_.reserve(passes);

    for (int i = 0; i < passes; ++i)
    {
        auto api = std::make_unique<tesseract::TessBaseAPI>();
        int rc = api->Init(tessdataPath_.c_str(), "eng", tesseract::OEM_LSTM_ONLY);
        if (rc != 0)
            return false;

        SetupTesseractApi(*api);

        workerApis_.push_back(std::move(api));
    }

    return true;
}

std::vector<double> OCR::BuildThresholds() const
{
    int passes = config_ ? config_->ocrPasses : 1;

    switch (passes)
    {
    case 1:
        return { 130.0 };

    case 2:
        return { 60.0, 130.0 };

    case 3:
        return { 30.0, 60.0, 130.0 };

    case 4:
        return { 30.0, 60.0, 130.0, 180.0 };

    case 5:
        return { 20.0, 30.0, 60.0, 130.0, 180.0 };

    case 6:
        return { 20.0, 30.0, 60.0, 130.0, 180.0, 220.0 };

    default:
        return { 130.0 };
    }
}

cv::Mat OCR::PrepareImage(const cv::Mat& img)
{
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    cv::Mat resized;
    cv::resize(gray, resized, {},  config_->ocrScale, config_->ocrScale, cv::INTER_LINEAR);

    cv::Mat thresh;
    cv::threshold(resized, thresh, config_->ocrThreshold, 255,  cv::THRESH_BINARY);

    return thresh;
}

bool OCR::IsSamePreparedImage(const cv::Mat& img)
{
    if (lastPrepared_.empty())
        return false;

    if (lastPrepared_.size() != img.size())
        return false;

    cv::Mat diff;
    cv::absdiff(img, lastPrepared_, diff);

    double changed = cv::countNonZero(diff);

    double ratio = changed / static_cast<double>(img.total());

    return ratio < 0.01;
}

std::vector<LootLine> OCR::RecognizeLoot2(const cv::Mat& img)
{
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    cv::Mat diffPrepared;
    cv::threshold(gray, diffPrepared, config_->ocrThreshold, 255, cv::THRESH_BINARY);

    if (IsSamePreparedImage(diffPrepared))
        return lastResult_;

    std::vector<LootLine> best;
    cv::Mat bestPrepared;

    for (double threshold : BuildThresholds())
    {
        cv::Mat prepared;
        cv::threshold(gray, prepared, threshold, 255, cv::THRESH_BINARY);

        auto result = RecognizePrepared(prepared);

        if (result.size() > best.size())
        {
            best = std::move(result);
            bestPrepared = prepared.clone();
        }

        if (best.size() >= 3)
            break;
    }

    lastPrepared_ = diffPrepared.clone();
    lastResult_ = best;

    return lastResult_;
}

std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& img)
{
    if (!initialized_ || img.empty())
        return {};

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    auto thresholds = BuildThresholds();
    if (thresholds.empty())
        return {};

    const size_t passCount = (std::min)(thresholds.size(), workerApis_.size());

    std::vector<std::future<std::vector<LootLine>>> futures;
    futures.reserve(passCount);
        
    for (size_t i = 0; i < passCount; ++i)
    {
        futures.emplace_back(
            std::async(
                std::launch::async,
                [this, gray, threshold = thresholds[i], i]()
                {
                    cv::Mat prepared;
                    cv::threshold(gray, prepared, threshold, 255, cv::THRESH_BINARY);
                    return RecognizePreparedWithApi(*workerApis_[i], prepared);
                }));
    }

    std::vector<LootLine> best;

    for (auto& f : futures)
    {
        auto result = f.get();
        if (result.size() > best.size())
            best = std::move(result);
    }

    return best;
}

std::vector<LootLine> OCR::RecognizePreparedWithApi(tesseract::TessBaseAPI& api, const cv::Mat& img)
{
    api.SetImage( img.data, img.cols, img.rows, 1, static_cast<int>(img.step));
    api.Recognize(nullptr);

    std::vector<LootLine> result;

    tesseract::ResultIterator* ri = api.GetIterator();
    if (!ri)
        return result;

    do
    {
        char* text = ri->GetUTF8Text(tesseract::RIL_TEXTLINE);
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

        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;

        if (!ri->BoundingBox(tesseract::RIL_TEXTLINE, &x1, &y1, &x2, &y2))
            continue;

        result.push_back({line, x1, y1, x2, y2,conf});

    } while (ri->Next(tesseract::RIL_TEXTLINE));

    return result;
}

cv::Mat OCR::Preprocess(const cv::Mat& src)
{
    const double thresholdValue = config_ ? config_->ocrThreshold : 130.0;
    return Preprocess(src, thresholdValue);
}

cv::Mat OCR::Preprocess(const cv::Mat& src, double thresholdValue)
{
    const double scale = config_ ? config_->ocrScale : 1.0;

    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, {}, scale, scale, cv::INTER_CUBIC);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::erode(gray, gray, kernel);

    cv::threshold(gray, gray, thresholdValue, 255, cv::THRESH_BINARY);

    if (debug_ || (config_ && config_->debugOCR))
    {
        std::string filename = "ocr_debug_" + std::to_string((int)thresholdValue) + ".png";
        cv::imwrite(filename, gray);
    }

    return gray;
}

std::vector<LootLine> OCR::RecognizePrepared(const cv::Mat& img)
{
    auto result = RecognizePreparedWithApi(api_, img);

    if (debug_ || (config_ && config_->debugOCR))
        DebugTesseract();

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