#include "OCR.h"

#include <leptonica/allheaders.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "core/Logger.h"
#include "core/ThreadGuard.h"
#include "ocr/NameNormalizer.h"
#include "ocr/OcrRowCache.h"
#include "platform/PlatformPaths.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <thread>

namespace
{
constexpr std::size_t kMaxOcrWorkers = 8;

constexpr double kTargetRowHeight = 48.0;
constexpr double kMinRowScale = 2.0;
constexpr double kMaxRowScale = 5.0;

constexpr int kMaxNativeWidth = 750;
constexpr int kReducedWidth = 680;

constexpr double kReferenceTextP25 = 144.0;
constexpr double kReferenceTextP50 = 165.0;
constexpr double kReferenceTextP95 = 190.0;
constexpr double kTextLevelTolerance = 12.0;
constexpr double kHeldLevelTolerance = 6.0;

struct PreparedGray
{
    cv::Mat gray;
    double scale = 1.0;
    bool scaled = false;
    bool normalized = false;
    TextLevels levels;
};

TextLevels MeasureTextLevels(const cv::Mat& gray)
{
    const int left = gray.cols / 2;
    const int right = gray.cols * 85 / 100;
    const int top = gray.rows / 10;
    const int bottom = gray.rows * 9 / 10;

    std::array<int, 256> histogram{};

    for (int y = top; y < bottom; ++y)
    {
        const unsigned char* row = gray.ptr<unsigned char>(y);

        for (int x = left; x < right; ++x)
            ++histogram[row[x]];
    }

    const double total = static_cast<double>(right - left) * (bottom - top);

    auto percentile = [&histogram, total](double share)
    {
        double seen = 0.0;

        for (int level = 0; level < 256; ++level)
        {
            seen += histogram[level];

            if (seen >= share * total)
                return static_cast<double>(level);
        }

        return 255.0;
    };

    return { percentile(0.25), percentile(0.50), percentile(0.95) };
}

bool CloseLevels(const TextLevels& a, const TextLevels& b)
{
    return std::abs(a.p25 - b.p25) <= kHeldLevelTolerance && std::abs(a.p50 - b.p50) <= kHeldLevelTolerance &&
           std::abs(a.p95 - b.p95) <= kHeldLevelTolerance;
}

PreparedGray PrepareGray(const cv::Mat& source, const std::optional<TextLevels>& held)
{
    PreparedGray prepared;
    prepared.gray = source;

    if (source.cols > kMaxNativeWidth)
    {
        cv::Mat reduced;
        prepared.scale = static_cast<double>(kReducedWidth) / source.cols;
        cv::resize(source, reduced, cv::Size(), prepared.scale, prepared.scale, cv::INTER_AREA);

        prepared.gray = reduced;
        prepared.scaled = true;
    }

    prepared.levels = MeasureTextLevels(prepared.gray);

    if (held && CloseLevels(prepared.levels, *held))
    {
        prepared.levels = *held;
    }
    else
    {
        const bool drifted = std::abs(prepared.levels.p50 - kReferenceTextP50) > kTextLevelTolerance ||
                             std::abs(prepared.levels.p95 - kReferenceTextP95) > kTextLevelTolerance;

        if (!drifted || prepared.levels.p95 <= prepared.levels.p25)
            return prepared;
    }

    const double gain = (kReferenceTextP95 - kReferenceTextP25) / (prepared.levels.p95 - prepared.levels.p25);

    cv::Mat lut(1, 256, CV_8U);

    for (int level = 0; level < 256; ++level)
        lut.at<unsigned char>(level) = cv::saturate_cast<unsigned char>(kReferenceTextP25 + (level - prepared.levels.p25) * gain);

    cv::Mat normalized;
    cv::LUT(prepared.gray, lut, normalized);

    prepared.gray = normalized;
    prepared.normalized = true;

    return prepared;
}

int ToSource(int value, double scale)
{
    return static_cast<int>(std::lround(value / scale));
}

std::size_t OcrWorkerCount()
{
    const unsigned hardware = std::thread::hardware_concurrency();

    if (hardware <= 1)
        return 1;

    return (std::min)(kMaxOcrWorkers, static_cast<std::size_t>(hardware / 2));
}
}

bool OCR::Init(std::string_view traineddata)
{
    LOG_INFO("OCR::Init traineddata = " + std::to_string(traineddata.size()) + " bytes");

    setMsgSeverity(L_SEVERITY_NONE);

    if (traineddata.empty())
    {
        LOG_ERROR("OCR::Init: no traineddata to load");
        return false;
    }

    std::vector<std::unique_ptr<tesseract::TessBaseAPI>> apis;

    for (std::size_t i = 0; i < OcrWorkerCount(); ++i)
    {
        auto api = std::make_unique<tesseract::TessBaseAPI>();
        const int rc = api->Init(
            traineddata.data(),
            static_cast<int>(traineddata.size()),
            "eng",
            tesseract::OEM_LSTM_ONLY,
            nullptr,
            0,
            nullptr,
            nullptr,
            false,
            nullptr
        );

        if (rc != 0)
        {
            if (apis.empty())
            {
                LOG_ERROR("Tesseract api.Init failed, rc=" + std::to_string(rc));
                return false;
            }

            LOG_ERROR("Tesseract worker api.Init failed, rc=" + std::to_string(rc));
            break;
        }

        SetupTesseractApi(*api);
        apis.push_back(std::move(api));
    }

    {
        std::lock_guard lock(apiMutex_);
        apis_ = std::move(apis);
    }

    initialized_ = true;
    LOG_INFO("OCR initialized, workers: " + std::to_string(apis_.size()));

    return true;
}

void OCR::SetupTesseractApi(tesseract::TessBaseAPI& api)
{
    // api.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
    api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);

    api.SetVariable(
        "tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789"
        " '-()"
    );

    api.SetVariable("preserve_interword_spaces", "1");
}

static std::filesystem::path PrepareOcrDebugDir()
{
    std::filesystem::path dir = GetUserDataDir() / "ocr_debug" / "latest";

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

    LOG_INFO("OCR debug screenshots: " + PathToUtf8(dir));
    return dir;
}

static bool SaveOcrDebugImage(const std::filesystem::path& path, const cv::Mat& img)
{
    if (path.empty() || img.empty())
        return false;

    std::vector<unsigned char> png;

    try
    {
        if (!cv::imencode(".png", img, png))
            return false;
    }
    catch (const cv::Exception& ex)
    {
        LOG_ERROR("OCR debug imencode failed: " + PathToUtf8(path) + " - " + ex.what());
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));

    return static_cast<bool>(file);
}

static void SaveOcrDebugText(
    const std::filesystem::path& debugBinPath,
    const std::string& rawText,
    const std::string& trimmedText,
    int confidence,
    const char* status
)
{
    if (debugBinPath.empty())
        return;

    std::filesystem::path path = debugBinPath;
    path.replace_extension(".txt");

    std::ofstream file(path);
    if (!file)
        return;

    file << "status: " << status << '\n';
    file << "confidence: " << confidence << '\n';
    file << "raw: " << rawText << '\n';
    file << "trimmed: " << trimmedText << '\n';
}

static std::filesystem::path OcrDebugRowPath(const std::filesystem::path& dir, size_t index, const char* suffix)
{
    std::ostringstream name;
    name << "row_" << std::setw(2) << std::setfill('0') << index << "_" << suffix << ".png";
    return dir / name.str();
}

static std::vector<int> FindTextStartX(const cv::Mat& gray, const std::vector<cv::Rect>& rows)
{
    std::vector<int> starts(rows.size(), 0);

    if (gray.empty() || rows.empty())
        return starts;

    constexpr double kGapBias = 0.5;

    std::vector<int> known;

    for (size_t i = 0; i < rows.size(); ++i)
    {
        cv::Mat dark;
        cv::threshold(gray(rows[i]), dark, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

        cv::Mat columns;
        cv::reduce(dark, columns, 0, cv::REDUCE_MAX, CV_8U);

        const unsigned char* columnInk = columns.ptr<unsigned char>(0);

        int lastInk = -1;

        for (int x = columns.cols - 1; x >= 0; --x)
        {
            if (columnInk[x] != 0)
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
            if (columnInk[x] == 0)
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

static int RightmostInkColumn(const cv::Mat& dark, int minimumRows)
{
    cv::Mat columnSums;
    cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

    const int* sums = columnSums.ptr<int>(0);
    const int limit = minimumRows * 255;

    for (int x = columnSums.cols - 1; x >= 0; --x)
    {
        if (sums[x] >= limit)
            return x;
    }

    return -1;
}

std::vector<cv::Rect> OCR::FindLootRows(const cv::Mat& gray) const
{
    std::vector<cv::Rect> rows;

    if (gray.empty())
        return rows;

    const int textAreaX = static_cast<int>(gray.cols * 0.50);
    cv::Mat rightGray = gray(cv::Rect(textAreaX, 0, gray.cols - textAreaX, gray.rows));

    cv::Mat dark;
    cv::threshold(rightGray, dark, 115, 255, cv::THRESH_BINARY_INV);

    constexpr double kVerticalLineInkRatio = 0.95;
    constexpr double kFrameInkRatio = 0.50;
    constexpr int kFrameSearchPercent = 15;

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 2));
    cv::morphologyEx(dark, dark, cv::MORPH_CLOSE, kernel);

    {
        cv::Mat columnSums;
        cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

        const int limit = static_cast<int>(dark.rows * 255.0 * kVerticalLineInkRatio);
        const int* sums = columnSums.ptr<int>(0);

        for (int x = 0; x < dark.cols; ++x)
        {
            if (sums[x] >= limit)
                dark.col(x).setTo(0);
        }
    }

    {
        cv::Mat columnSums;
        cv::reduce(dark, columnSums, 0, cv::REDUCE_SUM, CV_32S);

        const int limit = static_cast<int>(dark.rows * 255.0 * kFrameInkRatio);
        const int* sums = columnSums.ptr<int>(0);

        for (int x = dark.cols - dark.cols * kFrameSearchPercent / 100; x < dark.cols; ++x)
        {
            if (sums[x] >= limit)
            {
                dark.colRange(std::max(0, x - 1), dark.cols).setTo(0);
                break;
            }
        }
    }

    constexpr int kMinInkPerRow = 12;
    constexpr int kMaxBlankGap = 4;
    constexpr int kMinTextBandHeight = 6;
    constexpr double kMaxTextBandHeightFactor = 2.0;
    constexpr int kPaddingAbovePercent = 66;
    constexpr int kPaddingBelowPercent = 33;
    constexpr int kMinVerticalPadding = 2;
    constexpr double kMaxTextBandInkRatio = 0.25;
    constexpr int kTextEdgeRowPercent = 20;
    constexpr double kTextEdgeTolerance = 0.08;

    std::vector<std::pair<int, int>> bands;

    cv::Mat rowInk;
    cv::reduce(dark, rowInk, 1, cv::REDUCE_SUM, CV_32S);

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
        const int ink = rowInk.ptr<int>(y)[0] / 255;
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

    std::vector<int> rightEdges(bands.size(), -1);
    int textRightEdge = -1;

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const int h = bands[i].second - bands[i].first + 1;
        const int minimumRows = std::max(1, (h * kTextEdgeRowPercent + 99) / 100);

        rightEdges[i] = RightmostInkColumn(dark(cv::Rect(0, bands[i].first, dark.cols, h)), minimumRows);

        if (h >= kMinTextBandHeight)
            textRightEdge = std::max(textRightEdge, rightEdges[i]);
    }

    const int alignedFrom = textRightEdge - static_cast<int>(dark.cols * kTextEdgeTolerance);

    auto reachesTextEdge = [&rightEdges, alignedFrom](std::size_t band)
    { return rightEdges[band] >= 0 && rightEdges[band] >= alignedFrom; };

    std::vector<int> heights;
    heights.reserve(bands.size());

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const int h = bands[i].second - bands[i].first + 1;

        if (h >= kMinTextBandHeight && reachesTextEdge(i))
            heights.push_back(h);
    }

    std::sort(heights.begin(), heights.end());

    const int median = heights.empty() ? 0 : heights[heights.size() / 2];
    const int maxHeight = median > 0 ? static_cast<int>(median * kMaxTextBandHeightFactor) : gray.rows;

    for (std::size_t i = 0; i < bands.size(); ++i)
    {
        const auto& band = bands[i];
        const int h = band.second - band.first + 1;

        if (h < kMinTextBandHeight || h > maxHeight || !reachesTextEdge(i))
            continue;

        const int reference = median > 0 ? median : h;
        const int padAbove = std::max(kMinVerticalPadding, reference * kPaddingAbovePercent / 100);
        const int padBelow = std::max(kMinVerticalPadding, reference * kPaddingBelowPercent / 100);

        const int y = std::max(0, band.first - padAbove);
        const int y2 = (std::min)(gray.rows, band.second + padBelow + 1);
        const cv::Rect rect(0, y, dark.cols, y2 - y);
        const double inkRatio = static_cast<double>(cv::countNonZero(dark(rect))) / rect.area();

        if (inkRatio <= kMaxTextBandInkRatio)
            rows.push_back(cv::Rect(0, y, gray.cols, y2 - y));
    }

    return rows;
}

static cv::Mat TrimTrailingBlock(const cv::Mat& bin)
{
    constexpr double kBlockInkShare = 0.45;
    constexpr int kMinGapDivisor = 8;
    constexpr int kMinGapColumns = 6;

    if (bin.empty() || bin.cols < 16)
        return bin;

    cv::Mat dark;
    cv::threshold(bin, dark, 127, 1, cv::THRESH_BINARY_INV);

    cv::Mat inkPerColumn;
    cv::reduce(dark, inkPerColumn, 0, cv::REDUCE_SUM, CV_32S);

    const int* ink = inkPerColumn.ptr<int>(0);

    int end = bin.cols - 1;

    while (end >= 0 && ink[end] == 0)
        --end;

    if (end < 0)
        return bin;

    const int gap = std::max(kMinGapColumns, bin.rows / kMinGapDivisor);

    int x = end;
    int blank = 0;

    while (x >= 0)
    {
        if (ink[x] == 0)
        {
            if (++blank >= gap)
                break;
        }
        else
        {
            blank = 0;
        }

        --x;
    }

    if (x < 0)
        return bin;

    const int start = x + blank + 1;

    if (start > end)
        return bin;

    double total = 0.0;

    for (int i = start; i <= end; ++i)
        total += static_cast<double>(ink[i]) / bin.rows;

    if (total / (end - start + 1) <= kBlockInkShare)
        return bin;

    return bin(cv::Rect(0, 0, start, bin.rows));
}

std::vector<LootLine> OCR::RecognizeTextOnly(
    tesseract::TessBaseAPI& api,
    const cv::Mat& textGray,
    const std::filesystem::path& debugBinPath
)
{
    std::vector<LootLine> result;

    if (textGray.empty())
        return result;

    const double scale = std::clamp(std::round(kTargetRowHeight / static_cast<double>(textGray.rows)), kMinRowScale, kMaxRowScale);

    cv::Mat scaled;
    cv::resize(textGray, scaled, cv::Size(), scale, scale, cv::INTER_CUBIC);

    cv::Mat bin;
    cv::threshold(scaled, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    bin = TrimTrailingBlock(bin);

    if (!debugBinPath.empty())
        SaveOcrDebugImage(debugBinPath, bin);

    api.SetPageSegMode(tesseract::PSM_SINGLE_LINE);

    api.SetImage(bin.data, bin.cols, bin.rows, 1, static_cast<int>(bin.step));

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

    result.push_back({ line, 0, 0, textGray.cols, textGray.rows, static_cast<float>(conf) });

    return result;
}

void OCR::ReportPreparation(bool scaled, bool normalized, int sourceWidth, int readWidth, double p50, double p95)
{
    if (scaled != readScaled_)
    {
        readScaled_ = scaled;

        if (scaled)
        {
            LOG_INFO(
                "OCR: the region is " + std::to_string(sourceWidth) + " px wide, text is read at " + std::to_string(readWidth) + " px"
            );
        }
        else
        {
            LOG_INFO("OCR: the region is read at its own size");
        }
    }

    if (normalized != readNormalized_)
    {
        readNormalized_ = normalized;

        if (normalized)
        {
            LOG_INFO(
                "OCR: text brightness is off (median " + std::to_string(static_cast<int>(p50)) + ", highlights " +
                std::to_string(static_cast<int>(p95)) + "), it is normalised before reading"
            );
        }
        else
        {
            LOG_INFO("OCR: text brightness is back in the expected range");
        }
    }
}

std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& source, const AppConfig& config, OcrRowCache* rowCache)
{
    setMsgSeverity(config.debugOCR ? L_SEVERITY_INFO : L_SEVERITY_NONE);

    std::vector<LootLine> result;

    if (!initialized_ || source.empty())
        return result;

    std::unique_lock lock(apiMutex_);
    if (apis_.empty())
        return result;

    const PreparedGray prepared = PrepareGray(source, rowCache ? rowCache->Levels() : std::nullopt);
    const cv::Mat& gray = prepared.gray;

    if (rowCache)
        rowCache->SetLevels(prepared.normalized ? std::optional<TextLevels>(prepared.levels) : std::nullopt);

    ReportPreparation(prepared.scaled, prepared.normalized, source.cols, gray.cols, prepared.levels.p50, prepared.levels.p95);

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
            SaveOcrDebugImage(debugDir / "source.png", source);

            if (prepared.scaled || prepared.normalized)
                SaveOcrDebugImage(debugDir / "prepared.png", gray);

            cv::cvtColor(gray, debugRows, cv::COLOR_GRAY2BGR);
        }
    }

    auto rows = FindLootRows(gray);
    const std::vector<int> textStarts = FindTextStartX(gray, rows);

    if (debugOCR)
        rowCache = nullptr;

    struct RowJob
    {
        cv::Mat textGray;
        cv::Mat anchor;
        std::filesystem::path debugBinPath;
        int offsetX = 0;
        int offsetY = 0;
        bool reused = false;
        std::vector<LootLine> lines;
    };

    std::vector<RowJob> jobs(rows.size());

    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const cv::Rect& rowRect = rows[rowIndex];
        cv::Mat rowGray = gray(rowRect);

        const int textX = textStarts[rowIndex];
        const cv::Rect textRect(textX, 0, rowGray.cols - textX, rowGray.rows);

        RowJob& job = jobs[rowIndex];
        job.textGray = rowGray(textRect);
        job.offsetX = rowRect.x + textX;
        job.offsetY = rowRect.y;

        if (rowCache)
        {
            if (const OcrRowCache::Row* cached = rowCache->Find(job.textGray))
            {
                job.lines = cached->lines;
                job.anchor = cached->textGray;
                job.reused = true;
            }
        }

        if (!debugOCR)
            continue;

        SaveOcrDebugImage(OcrDebugRowPath(debugDir, rowIndex, "row"), gray(rowRect));
        SaveOcrDebugImage(OcrDebugRowPath(debugDir, rowIndex, "text"), gray(rowRect)(textRect));
        job.debugBinPath = OcrDebugRowPath(debugDir, rowIndex, "bin");

        cv::rectangle(debugRows, rowRect, cv::Scalar(0, 255, 0), 2);
        cv::line(
            debugRows,
            cv::Point(job.offsetX, rowRect.y),
            cv::Point(job.offsetX, rowRect.y + rowRect.height),
            cv::Scalar(255, 0, 0),
            2
        );
    }

    std::vector<size_t> pending;
    pending.reserve(jobs.size());

    for (size_t i = 0; i < jobs.size(); ++i)
    {
        if (!jobs[i].reused)
            pending.push_back(i);
    }

    const size_t workers = (std::min)(apis_.size(), pending.size());

    std::atomic<bool> rowErrorLogged{ false };

    auto runRow = [this, &rowErrorLogged](RowJob& job, tesseract::TessBaseAPI& api)
    {
        try
        {
            job.lines = RecognizeTextOnly(api, job.textGray, job.debugBinPath);
        }
        catch (const std::exception& error)
        {
            if (!rowErrorLogged.exchange(true))
                LOG_ERROR(std::string("OCR row failed: ") + error.what());
        }
        catch (...)
        {
            if (!rowErrorLogged.exchange(true))
                LOG_ERROR("OCR row failed with an exception of unknown type");
        }
    };

    if (workers <= 1)
    {
        for (size_t i : pending)
            runRow(jobs[i], *apis_[0]);
    }
    else
    {
        std::atomic<size_t> next{ 0 };
        std::vector<std::jthread> pool;
        pool.reserve(workers);

        for (size_t worker = 0; worker < workers; ++worker)
        {
            pool.emplace_back(
                [this, worker, &next, &jobs, &pending, &runRow]
                {
                    RunLoggingExceptions(
                        "OCR worker",
                        [&]
                        {
                            for (size_t i = next++; i < pending.size(); i = next++)
                                runRow(jobs[pending[i]], *apis_[worker]);
                        }
                    );
                }
            );
        }
    }

    if (rowCache)
    {
        std::vector<OcrRowCache::Row> generation;
        generation.reserve(jobs.size());

        for (const RowJob& job : jobs)
            generation.push_back({ job.reused ? job.anchor : job.textGray.clone(), job.lines });

        rowCache->Store(std::move(generation));
    }

    for (RowJob& job : jobs)
    {
        for (LootLine& line : job.lines)
        {
            line.x1 = ToSource(line.x1 + job.offsetX, prepared.scale);
            line.x2 = ToSource(line.x2 + job.offsetX, prepared.scale);
            line.y1 = ToSource(line.y1 + job.offsetY, prepared.scale);
            line.y2 = ToSource(line.y2 + job.offsetY, prepared.scale);

            result.push_back(std::move(line));
        }
    }

    if (debugOCR)
        SaveOcrDebugImage(debugDir / "rows_detected.png", debugRows);

    return result;
}

void OCR::Trim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));

    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
}
