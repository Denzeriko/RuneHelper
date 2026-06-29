#include "OCR.h"

#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <filesystem>
#include <regex>

OCR::~OCR()
{
    Reset();
}

void OCR::Reset()
{
    if (!initialized_)
        return;

    api_.End();
    initialized_ = false;
    appliedPsm_ = -1;
}

bool OCR::Init(const std::string& tessdataPath)
{
    Reset();

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
    #ifdef _WIN32
    api_.SetVariable("debug_file", "NUL");
    #else
    api_.SetVariable("debug_file", "/dev/null");
    #endif
    api_.SetVariable("classify_bln_numeric_mode", "0");
    api_.SetVariable("preserve_interword_spaces", "1");
    api_.SetVariable(
        "tessedit_char_whitelist",
        "0123456789xABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz() -'"
    );

    ApplyRuntimeConfig(true);

    LOG_INFO("SetupTesseract done");
}

void OCR::ApplyRuntimeConfig(bool force)
{
    int psm = config_ ? config_->ocrPsm : static_cast<int>(tesseract::PSM_SPARSE_TEXT);

    if (!force && appliedPsm_ == psm)
        return;

    api_.SetPageSegMode(static_cast<tesseract::PageSegMode>(psm));
    appliedPsm_ = psm;

    LOG_INFO("OCR PSM set to " + std::to_string(psm));
}

std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& src)
{
    if (!initialized_ || src.empty())
        return {};

    ApplyRuntimeConfig();

    cv::Mat prepared = Preprocess(src);

    return RecognizePrepared(prepared);
}

std::vector<OcrRowDebug> OCR::RecognizeRows(const cv::Mat& src, const std::vector<int>& psms, const std::string& debugPrefix)
{
    if (!initialized_ || src.empty())
        return {};

    ApplyRuntimeConfig();

    cv::Mat prepared = Preprocess(src);
    std::vector<cv::Rect> rows = DetectRows(prepared);
    std::vector<OcrRowDebug> result;

    if (!debugPrefix.empty())
    {
        cv::imwrite(debugPrefix + "_full.png", src);
        cv::imwrite(debugPrefix + "_threshold.png", prepared);
    }

    if (rows.empty())
        rows.push_back(cv::Rect(0, 0, prepared.cols, prepared.rows));

    int rowIndex = 0;

    for (const cv::Rect& row : rows)
    {
        cv::Mat rowImg = prepared(row).clone();

        if (!debugPrefix.empty())
            cv::imwrite(debugPrefix + "_row_" + std::to_string(rowIndex) + ".png", rowImg);

        for (int psm : psms)
        {
            float confidence = 0.0f;
            std::string raw = RecognizeRowText(rowImg, psm, &confidence);
            std::string processed = PostProcessText(raw);

            if (processed.empty())
                continue;

            result.push_back(OcrRowDebug{
                row,
                psm,
                raw,
                processed,
                confidence
            });

            break;
        }

        ++rowIndex;
    }

    int configuredPsm = config_ ? config_->ocrPsm : static_cast<int>(tesseract::PSM_SPARSE_TEXT);
    api_.SetPageSegMode(static_cast<tesseract::PageSegMode>(configuredPsm));
    return result;
}

cv::Mat OCR::PreprocessForDebug(const cv::Mat& src)
{
    return Preprocess(src);
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

std::vector<cv::Rect> OCR::DetectRows(const cv::Mat& prepared) const
{
    if (prepared.empty())
        return {};

    cv::Mat binary;

    if (prepared.channels() == 1)
        binary = prepared;
    else
        cv::cvtColor(prepared, binary, cv::COLOR_BGR2GRAY);

    int totalWhite = 0;
    int totalBlack = 0;

    for (int y = 0; y < binary.rows; ++y)
    {
        const unsigned char* row = binary.ptr<unsigned char>(y);

        for (int x = 0; x < binary.cols; ++x)
        {
            if (row[x] > 0)
                ++totalWhite;
            else
                ++totalBlack;
        }
    }

    const bool foregroundIsWhite = totalWhite <= totalBlack;
    std::vector<int> rowCounts(binary.rows, 0);

    for (int y = 0; y < binary.rows; ++y)
    {
        const unsigned char* row = binary.ptr<unsigned char>(y);

        for (int x = 0; x < binary.cols; ++x)
        {
            bool isForeground = foregroundIsWhite ? row[x] > 0 : row[x] == 0;

            if (isForeground)
                ++rowCounts[y];
        }
    }

    const int minInk = std::max(3, binary.cols / 80);
    const int minHeight = std::max(6, static_cast<int>((config_ ? config_->ocrScale : 1.0f) * 8.0f));
    const int padY = 3;
    std::vector<cv::Rect> rows;

    bool inBand = false;
    int start = 0;

    for (int y = 0; y < binary.rows; ++y)
    {
        bool hasInk = rowCounts[y] >= minInk;

        if (hasInk && !inBand)
        {
            inBand = true;
            start = y;
        }
        else if (!hasInk && inBand)
        {
            int end = y - 1;

            if (end - start + 1 >= minHeight)
            {
                int top = std::max(0, start - padY);
                int bottom = std::min(binary.rows - 1, end + padY);
                rows.emplace_back(0, top, binary.cols, bottom - top + 1);
            }

            inBand = false;
        }
    }

    if (inBand)
    {
        int end = binary.rows - 1;

        if (end - start + 1 >= minHeight)
        {
            int top = std::max(0, start - padY);
            int bottom = std::min(binary.rows - 1, end + padY);
            rows.emplace_back(0, top, binary.cols, bottom - top + 1);
        }
    }

    std::vector<cv::Rect> merged;
    const int mergeGap = std::max(3, minHeight / 2);

    for (const cv::Rect& row : rows)
    {
        if (!merged.empty() && row.y - (merged.back().y + merged.back().height) <= mergeGap)
        {
            int top = merged.back().y;
            int bottom = std::max(merged.back().y + merged.back().height, row.y + row.height);
            merged.back() = cv::Rect(0, top, binary.cols, bottom - top);
        }
        else
        {
            merged.push_back(row);
        }
    }

    return merged;
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

        line = PostProcessText(line);

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

std::string OCR::RecognizeRowText(const cv::Mat& img, int psm, float* confidence)
{
    if (confidence)
        *confidence = 0.0f;

    if (img.empty())
        return {};

    api_.SetPageSegMode(static_cast<tesseract::PageSegMode>(psm));
    api_.SetImage(img.data, img.cols, img.rows, 1, static_cast<int>(img.step));
    api_.Recognize(nullptr);

    if (confidence)
    {
        tesseract::ResultIterator* ri = api_.GetIterator();

        if (ri)
            *confidence = ri->Confidence(tesseract::RIL_TEXTLINE);
    }

    char* raw = api_.GetUTF8Text();

    if (!raw)
        return {};

    std::string text(raw);
    delete[] raw;

    std::replace(text.begin(), text.end(), '\n', ' ');
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    Trim(text);
    return text;
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

std::string OCR::PostProcessText(std::string text)
{
    std::replace(text.begin(), text.end(), '\n', ' ');
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

    std::string filtered;
    filtered.reserve(text.size());

    for (unsigned char c : text)
    {
        if (std::isalnum(c) || c == ' ' || c == '\'' || c == '-' || c == '(' || c == ')')
            filtered.push_back(static_cast<char>(c));
    }

    text = std::move(filtered);
    text = std::regex_replace(text, std::regex(R"(\s+)"), " ");
    text = std::regex_replace(text, std::regex(R"(^\s*[lI]\s*[xX]\s+)"), "1x ");
    text = std::regex_replace(text, std::regex(R"(^\s*([0-9]+)\s*[xX]\s*)"), "$1x ");

    Trim(text);

    return text;
}
