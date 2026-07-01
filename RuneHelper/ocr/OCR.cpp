#include "OCR.h"

#include "core/Logger.h"
#include "ocr/NameNormalizer.h"

#include <algorithm>
#include <cctype>
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

bool OCR::ReinitializeWorkers(const AppConfig& config)
{
    std::lock_guard lock(workerMutex_);

    workerApis_.clear();

    int passes = std::clamp(config.ocrPasses, 1, 6);

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

std::vector<double> OCR::BuildThresholds(const AppConfig& config)
{
    int passes = config.ocrPasses;

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

std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& img, const AppConfig& config)
{
    if (!initialized_ || img.empty())
        return {};

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    auto thresholds = BuildThresholds(config);
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
