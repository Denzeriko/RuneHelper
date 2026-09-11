#include "OCR.h"

#include <leptonica/allheaders.h>

#include "core/Logger.h"
#include "ocr/NameNormalizer.h"
#include "ocr/RunePatternMatcher.h"
#include "platform/PlatformPaths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

bool OCR::Init(const std::string& tessdataPath)
{
    LOG_INFO("OCR::Init tessdataPath = " + tessdataPath);

    setMsgSeverity(L_SEVERITY_NONE);

    tessdataPath_ = tessdataPath;
    std::filesystem::path engPath = std::filesystem::path(tessdataPath) / "eng.traineddata";

    if (!std::filesystem::exists(engPath))
    {
        LOG_ERROR("eng.traineddata not found: " + engPath.string());
        return false;
    }

    auto api = std::make_unique<tesseract::TessBaseAPI>();
    int rc = api->Init(tessdataPath_.c_str(), "eng", tesseract::OEM_LSTM_ONLY);
    if (rc != 0)
    {
        LOG_ERROR("Tesseract api.Init failed, rc=" + std::to_string(rc));
        return false;
    }

    SetupTesseractApi(*api);

    {
        std::lock_guard lock(apiMutex_);
        api_ = std::move(api);
    }

    initialized_ = true;
    LOG_INFO("OCR initialized");

    return true;
}

void OCR::SetupTesseractApi(tesseract::TessBaseAPI& api)
{
    //api.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
    api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);

    api.SetVariable(
        "tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        " '-"
    );

    api.SetVariable("preserve_interword_spaces", "1");
}

static std::filesystem::path PrepareOcrDebugDir()
{
    std::filesystem::path dir = GetAppDataDir() / "ocr_debug" / "latest";

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (ec)
        LOG_ERROR("OCR debug remove_all failed: " + ec.message());

    ec.clear();
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        LOG_ERROR("OCR debug create_directories failed: " + ec.message());
        return {};
    }

    LOG_INFO("OCR debug screenshots: " + dir.string());
    return dir;
}

static bool SaveOcrDebugImage(const std::filesystem::path& path, const cv::Mat& img)
{
    if (path.empty() || img.empty())
        return false;

    try
    {
        return cv::imwrite(path.string(), img);
    }
    catch (const cv::Exception& ex)
    {
        LOG_ERROR("OCR debug imwrite failed: " + path.string() + " - " + ex.what());
        return false;
    }
}

static void SaveOcrDebugText(
    const std::string& debugBinPath,
    const std::string& rawText,
    const std::string& trimmedText,
    int confidence,
    const char* status)
{
    if (debugBinPath.empty())
        return;

    std::filesystem::path path(debugBinPath);
    path.replace_extension(".txt");

    std::ofstream file(path);
    if (!file)
        return;

    file << "status: " << status << '\n';
    file << "confidence: " << confidence << '\n';
    file << "raw: " << rawText << '\n';
    file << "trimmed: " << trimmedText << '\n';
}

static std::filesystem::path OcrDebugRowPath(
    const std::filesystem::path& dir,
    size_t index,
    const char* suffix)
{
    std::ostringstream name;
    name << "row_" << std::setw(2) << std::setfill('0') << index << "_" << suffix << ".png";
    return dir / name.str();
}

static void SaveRunePatternDebugText(const std::filesystem::path& dir, const std::vector<RunePatternMatch>& matches)
{
    if (dir.empty())
        return;

    std::ofstream file(dir / "rune_matches.txt");
    if (!file)
        return;

    file << "count: " << matches.size() << '\n';

    for (const auto& match : matches)
    {
        file << match.name
             << " label=" << match.label
             << " x=" << match.rect.x
             << " y=" << match.rect.y
             << " w=" << match.rect.width
             << " h=" << match.rect.height
             << " score=" << match.score
             << " scale=" << match.scale
             << '\n';
    }
}

static std::vector<int> FindTextStartX(const cv::Mat& img, const std::vector<cv::Rect>& rows)
{
    std::vector<int> starts(rows.size(), 0);

    if (img.empty() || rows.empty())
        return starts;

    constexpr double kGapBias = 0.5;

    std::vector<int> known;

    for (size_t i = 0; i < rows.size(); ++i)
    {
        cv::Mat gray;
        cv::cvtColor(img(rows[i]), gray, cv::COLOR_BGR2GRAY);

        cv::Mat dark;
        cv::threshold(gray, dark, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

        cv::Mat columns;
        cv::reduce(dark, columns, 0, cv::REDUCE_MAX, CV_8U);

        int lastInk = -1;

        for (int x = columns.cols - 1; x >= 0; --x)
        {
            if (columns.at<unsigned char>(0, x) != 0)
            {
                lastInk = x;
                break;
            }
        }

        int bestStart = -1;
        int bestWidth = 0;
        int runStart = -1;

        for (int x = 0; x <= lastInk; ++x)
        {
            if (columns.at<unsigned char>(0, x) == 0)
            {
                if (runStart < 0)
                    runStart = x;

                continue;
            }

            if (runStart >= 0 && x - runStart > bestWidth)
            {
                bestWidth = x - runStart;
                bestStart = runStart;
            }

            runStart = -1;
        }

        if (bestStart < 0)
        {
            starts[i] = -1;
            continue;
        }

        starts[i] = bestStart + static_cast<int>(bestWidth * kGapBias);
        known.push_back(starts[i]);
    }

    int fallback = 0;

    if (!known.empty())
    {
        std::sort(known.begin(), known.end());
        fallback = known[known.size() / 2];
    }

    for (int& value : starts)
    {
        if (value < 0)
            value = fallback;
    }

    return starts;
}

std::vector<cv::Rect> OCR::FindLootRows(const cv::Mat& img) const
{
    std::vector<cv::Rect> rows;

    if (img.empty())
        return rows;

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    const int textAreaX = static_cast<int>(img.cols * 0.50);
    cv::Mat rightGray = gray(cv::Rect(textAreaX, 0, img.cols - textAreaX, img.rows));

    cv::Mat dark;
    cv::threshold(rightGray, dark, 115, 255, cv::THRESH_BINARY_INV);

    constexpr double kVerticalLineInkRatio = 0.95;

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 2));
    cv::morphologyEx(dark, dark, cv::MORPH_CLOSE, kernel);

    {
        cv::Mat columnSums;
        cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

        const int limit = static_cast<int>(dark.rows * 255.0 * kVerticalLineInkRatio);

        for (int x = 0; x < dark.cols; ++x)
        {
            if (columnSums.at<int>(0, x) >= limit)
                dark.col(x).setTo(0);
        }
    }

    constexpr int kMinInkPerRow = 12;
    constexpr int kMaxBlankGap = 4;
    constexpr int kMinTextBandHeight = 6;
    constexpr double kMaxTextBandHeightFactor = 2.0;
    constexpr int kVerticalPadding = 8;
    constexpr double kMaxTextBandInkRatio = 0.25;

    std::vector<std::pair<int, int>> bands;

    bool inBand = false;
    int bandStart = 0;
    int bandEnd = 0;
    int blankGap = 0;

    auto finishBand = [&]()
    {
        if (!inBand)
            return;

        bands.emplace_back(bandStart, bandEnd);
        inBand = false;
        blankGap = 0;
    };

    for (int y = 0; y < dark.rows; ++y)
    {
        const int ink = cv::countNonZero(dark.row(y));
        if (ink >= kMinInkPerRow)
        {
            if (!inBand)
            {
                inBand = true;
                bandStart = y;
            }

            bandEnd = y;
            blankGap = 0;
            continue;
        }

        if (!inBand)
            continue;

        ++blankGap;
        if (blankGap > kMaxBlankGap)
            finishBand();
    }

    finishBand();

    std::vector<int> heights;
    heights.reserve(bands.size());

    for (const auto& band : bands)
    {
        const int h = band.second - band.first + 1;

        if (h >= kMinTextBandHeight)
            heights.push_back(h);
    }

    std::sort(heights.begin(), heights.end());

    const int median = heights.empty() ? 0 : heights[heights.size() / 2];
    const int maxHeight = median > 0 ? static_cast<int>(median * kMaxTextBandHeightFactor) : img.rows;

    for (const auto& band : bands)
    {
        const int h = band.second - band.first + 1;

        if (h < kMinTextBandHeight || h > maxHeight)
            continue;

        const int y = std::max(0, band.first - kVerticalPadding);
        const int y2 = (std::min)(img.rows, band.second + kVerticalPadding + 1);
        const cv::Rect rect(0, y, dark.cols, y2 - y);
        const double inkRatio = static_cast<double>(cv::countNonZero(dark(rect))) / rect.area();

        if (inkRatio <= kMaxTextBandInkRatio)
            rows.push_back(cv::Rect(0, y, img.cols, y2 - y));
    }

    return rows;
}

std::vector<LootLine> OCR::RecognizeTextOnly(
    tesseract::TessBaseAPI& api,
    const cv::Mat& textBgr,
    const std::string& debugBinPath)
{
    std::vector<LootLine> result;

    if (textBgr.empty())
        return result;

    cv::Mat gray;
    cv::cvtColor(textBgr, gray, cv::COLOR_BGR2GRAY);

    cv::Mat scaled;
    cv::resize(gray, scaled, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);

    cv::Mat bin;
    cv::threshold(scaled, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    if (!debugBinPath.empty())
        SaveOcrDebugImage(debugBinPath, bin);

    api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);

    api.SetImage(
        bin.data,
        bin.cols,
        bin.rows,
        1,
        static_cast<int>(bin.step)
    );

    api.Recognize(nullptr);

    char* text = api.GetUTF8Text();
    int conf = api.MeanTextConf();

    if (!text)
    {
        SaveOcrDebugText(debugBinPath, "", "", conf, "rejected_no_text");
        return result;
    }

    std::string rawText(text);
    std::string line(rawText);
    delete[] text;

    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
    line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

    Trim(line);

    if (line.empty())
    {
        SaveOcrDebugText(debugBinPath, rawText, line, conf, "rejected_empty");
        return result;
    }

    if (conf < 15)
    {
        SaveOcrDebugText(debugBinPath, rawText, line, conf, "rejected_low_confidence");
        return result;
    }

    SaveOcrDebugText(debugBinPath, rawText, line, conf, "accepted");

    result.push_back({
        line,
        0,
        0,
        textBgr.cols,
        textBgr.rows,
        static_cast<float>(conf)
    });

    return result;
}
std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& img, const AppConfig& config)
{
    setMsgSeverity(config.debugOCR ? L_SEVERITY_INFO : L_SEVERITY_NONE);

    std::vector<LootLine> result;

    if (!initialized_ || img.empty())
        return result;

    std::unique_lock lock(apiMutex_);
    if (!api_)
        return result;

    tesseract::TessBaseAPI& api = *api_;

    bool debugOCR = config.debugOCR;
    std::filesystem::path debugDir;
    cv::Mat debugRows;

    if (debugOCR)
    {
        debugDir = PrepareOcrDebugDir();
        if (debugDir.empty())
        {
            debugOCR = false;
        }
        else
        {
            SaveOcrDebugImage(debugDir / "source.png", img);
            debugRows = img.clone();
        }
    }

    if (debugOCR)
    {
        auto runeMatches = FindRunePatternMatches(img);
        SaveRunePatternDebugText(debugDir, runeMatches);

        for (const auto& match : runeMatches)
        {
            cv::rectangle(debugRows, match.rect, cv::Scalar(0, 165, 255), 2);
            cv::putText(
                debugRows,
                match.name,
                cv::Point(match.rect.x, std::max(0, match.rect.y - 4)),
                cv::FONT_HERSHEY_SIMPLEX,
                0.45,
                cv::Scalar(0, 165, 255),
                1,
                cv::LINE_AA
            );
        }
    }

    auto rows = FindLootRows(img);
    const std::vector<int> textStarts = FindTextStartX(img, rows);

    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const auto& rowRect = rows[rowIndex];
        cv::Mat row = img(rowRect);

        if (debugOCR)
        {
            SaveOcrDebugImage(OcrDebugRowPath(debugDir, rowIndex, "row"), row);
            cv::rectangle(debugRows, rowRect, cv::Scalar(0, 255, 0), 2);
        }

        const int textX = textStarts[rowIndex];

        cv::Rect textRect(
            textX,
            0,
            row.cols - textX,
            row.rows
        );

        cv::Mat textCrop = row(textRect);

        std::string debugBinPath;
        if (debugOCR)
        {
            SaveOcrDebugImage(OcrDebugRowPath(debugDir, rowIndex, "text"), textCrop);
            debugBinPath = OcrDebugRowPath(debugDir, rowIndex, "bin").string();

            const int absoluteTextX = rowRect.x + textX;
            cv::line(
                debugRows,
                cv::Point(absoluteTextX, rowRect.y),
                cv::Point(absoluteTextX, rowRect.y + rowRect.height),
                cv::Scalar(255, 0, 0),
                2
            );
        }

        auto lines = RecognizeTextOnly(api, textCrop, debugBinPath);

        for (auto& line : lines)
        {
            line.x1 += rowRect.x + textX;
            line.x2 += rowRect.x + textX;
            line.y1 += rowRect.y;
            line.y2 += rowRect.y;

            result.push_back(std::move(line));
        }
    }

    if (debugOCR)
        SaveOcrDebugImage(debugDir / "rows_detected.png", debugRows);

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
