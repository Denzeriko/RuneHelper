#include "OCR.h"

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

constexpr float kMinReadConfidence = 80.0f;

constexpr int kMaxNativeWidth = 750;
constexpr int kReducedWidth = 680;

constexpr double kReferenceTextP25 = 144.0;
constexpr double kReferenceTextP50 = 165.0;
constexpr double kReferenceTextP95 = 190.0;
constexpr double kTextLevelTolerance = 12.0;
constexpr double kHeldLevelTolerance = 6.0;

constexpr int kMinPanelSide = 64;
constexpr int kMinPanelEdgeContrast = 12;
constexpr int kPanelEdgeSpan = 3;
constexpr double kPanelEdgeShare = 0.30;
constexpr double kPanelLeftEdgeShare = 0.50;
constexpr float kPanelRowDensity = 0.40f;
constexpr double kPanelMinMargin = 0.05;
constexpr int kHeldPanelTolerance = 2;

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
        prepared.scale = ReadingScale(source.cols);
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

    prepared.gray = NormalizeTextLevels(prepared.gray, prepared.levels);
    prepared.normalized = true;

    return prepared;
}

int EdgeThreshold(const cv::Mat& gray)
{
    std::array<int, 256> histogram{};

    for (int y = 0; y < gray.rows; ++y)
    {
        const unsigned char* row = gray.ptr<unsigned char>(y);

        for (int x = 0; x < gray.cols; ++x)
            ++histogram[row[x]];
    }

    auto percentile = [&histogram, &gray](double share)
    {
        const double wanted = share * static_cast<double>(gray.total());
        double seen = 0.0;

        for (int level = 0; level < 256; ++level)
        {
            seen += histogram[level];

            if (seen >= wanted)
                return level;
        }

        return 255;
    };

    return std::max(kMinPanelEdgeContrast, (percentile(0.95) - percentile(0.05)) / 5);
}

int StrongestColumn(const int* counts, int from, int to)
{
    int best = -1;

    for (int x = std::max(0, from); x < to; ++x)
    {
        if (best < 0 || counts[x] > counts[best])
            best = x;
    }

    return best;
}

cv::Rect FindPanel(const cv::Mat& gray)
{
    const cv::Rect whole(0, 0, gray.cols, gray.rows);

    if (gray.cols < kMinPanelSide || gray.rows < kMinPanelSide)
        return whole;

    const int threshold = EdgeThreshold(gray);

    cv::Mat smooth;
    cv::blur(gray, smooth, cv::Size(2 * kPanelEdgeSpan + 1, 1));

    const int width = gray.cols - 2 * kPanelEdgeSpan;

    cv::Mat change;
    cv::subtract(
        smooth(cv::Rect(0, 0, width, gray.rows)),
        smooth(cv::Rect(2 * kPanelEdgeSpan, 0, width, gray.rows)),
        change,
        cv::noArray(),
        CV_16S
    );

    const cv::Mat darkening = change > threshold;
    const cv::Mat brightening = change < -threshold;

    cv::Mat darkeningPerColumn;
    cv::Mat brighteningPerColumn;
    cv::reduce(darkening, darkeningPerColumn, 0, cv::REDUCE_SUM, CV_32S);
    cv::reduce(brightening, brighteningPerColumn, 0, cv::REDUCE_SUM, CV_32S);

    const int* darkeningRows = darkeningPerColumn.ptr<int>(0);
    const int* brighteningRows = brighteningPerColumn.ptr<int>(0);

    const int right = StrongestColumn(darkeningRows, width * 55 / 100, width);

    if (right < 0 || darkeningRows[right] < kPanelEdgeShare * 255.0 * gray.rows)
        return whole;

    std::vector<float> edgeRows(static_cast<std::size_t>(gray.rows), 0.0f);

    for (int y = 0; y < gray.rows; ++y)
    {
        const unsigned char* row = darkening.ptr<unsigned char>(y);

        for (int x = std::max(0, right - 1); x <= std::min(width - 1, right + 1); ++x)
        {
            if (row[x] != 0)
                edgeRows[static_cast<std::size_t>(y)] = 1.0f;
        }
    }

    cv::Mat density;
    cv::blur(
        cv::Mat(gray.rows, 1, CV_32F, edgeRows.data()),
        density,
        cv::Size(1, std::max(8, gray.rows / 16)),
        cv::Point(-1, -1),
        cv::BORDER_REPLICATE
    );

    int top = -1;
    int bottom = -1;

    for (int y = 0; y < gray.rows; ++y)
    {
        if (density.at<float>(y) < kPanelRowDensity)
            continue;

        if (top < 0)
            top = y;

        bottom = y;
    }

    if (top < 0)
        return whole;

    const int left = StrongestColumn(brighteningRows, 0, width / 4);
    const bool framedLeft = left >= 0 && brighteningRows[left] >= kPanelLeftEdgeShare * darkeningRows[right];

    const int panelLeft = framedLeft ? left + kPanelEdgeSpan : 0;
    const int panelRight = right + kPanelEdgeSpan;

    const int sideMargin = static_cast<int>(gray.cols * kPanelMinMargin);
    const int endMargin = static_cast<int>(gray.rows * kPanelMinMargin);

    const int cropLeft = panelLeft >= sideMargin ? panelLeft : 0;
    const int cropRight = gray.cols - panelRight >= sideMargin ? panelRight : gray.cols;
    const int cropTop = top >= endMargin ? top : 0;
    const int cropBottom = gray.rows - 1 - bottom >= endMargin ? bottom + 1 : gray.rows;

    return cv::Rect(cropLeft, cropTop, cropRight - cropLeft, cropBottom - cropTop) & whole;
}

bool ClosePanels(const cv::Rect& a, const cv::Rect& b)
{
    return std::abs(a.x - b.x) <= kHeldPanelTolerance && std::abs(a.y - b.y) <= kHeldPanelTolerance &&
           std::abs(a.br().x - b.br().x) <= kHeldPanelTolerance && std::abs(a.br().y - b.br().y) <= kHeldPanelTolerance;
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

double ReadingScale(int width)
{
    return width > kMaxNativeWidth ? static_cast<double>(kReducedWidth) / width : 1.0;
}

cv::Mat NormalizeTextLevels(const cv::Mat& gray, const TextLevels& levels)
{
    if (levels.p95 <= levels.p25)
        return gray;

    const double gain = (kReferenceTextP95 - kReferenceTextP25) / (levels.p95 - levels.p25);

    cv::Mat lut(1, 256, CV_8U);

    for (int level = 0; level < 256; ++level)
        lut.at<unsigned char>(level) = cv::saturate_cast<unsigned char>(kReferenceTextP25 + (level - levels.p25) * gain);

    cv::Mat normalized;
    cv::LUT(gray, lut, normalized);

    return normalized;
}

bool OCR::Init(std::string_view model)
{
    LOG_INFO("OCR::Init model = " + std::to_string(model.size()) + " bytes");

    std::lock_guard lock(mutex_);

    if (!reader_.Load(model))
    {
        LOG_ERROR("OCR::Init: the text model could not be loaded");
        return false;
    }

    workers_ = OcrWorkerCount();
    initialized_ = true;
    LOG_INFO("OCR initialized, workers: " + std::to_string(workers_));

    return true;
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
    const std::filesystem::path& debugPath,
    const std::string& rawText,
    const std::string& trimmedText,
    float confidence,
    const char* status
)
{
    if (debugPath.empty())
        return;

    std::filesystem::path path = debugPath;
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

std::vector<LootLine> OCR::RecognizeTextOnly(const cv::Mat& textGray, const std::filesystem::path& debugPath) const
{
    std::vector<LootLine> result;

    if (textGray.empty())
        return result;

    if (!debugPath.empty())
    {
        cv::Mat input;
        LineReader::Prepare(textGray).convertTo(input, CV_8U, 255.0);
        SaveOcrDebugImage(debugPath, input);
    }

    const LineReader::Result read = reader_.Read(textGray);
    std::string line = read.text;

    Trim(line);

    if (line.empty())
    {
        SaveOcrDebugText(debugPath, read.text, line, read.confidence, "rejected_empty");
        return result;
    }

    if (read.confidence < kMinReadConfidence)
    {
        SaveOcrDebugText(debugPath, read.text, line, read.confidence, "rejected_low_confidence");
        return result;
    }

    SaveOcrDebugText(debugPath, read.text, line, read.confidence, "accepted");

    result.push_back({ line, 0, 0, textGray.cols, textGray.rows, read.confidence });

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
                "OCR: the panel is " + std::to_string(sourceWidth) + " px wide, text is read at " + std::to_string(readWidth) + " px"
            );
        }
        else
        {
            LOG_INFO("OCR: the panel is read at its own size");
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

void OCR::ReportPanel(const cv::Rect& panel, const cv::Size& source)
{
    const bool trimmed = panel.size() != source;

    if (trimmed == readTrimmed_)
        return;

    readTrimmed_ = trimmed;

    if (trimmed)
    {
        LOG_INFO(
            "OCR: the region is " + std::to_string(source.width) + "x" + std::to_string(source.height) + ", only the " +
            std::to_string(panel.width) + "x" + std::to_string(panel.height) + " loot panel at " + std::to_string(panel.x) + "," +
            std::to_string(panel.y) + " inside it is read"
        );
    }
    else
    {
        LOG_INFO("OCR: the whole region is read");
    }
}

std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& source, const AppConfig& config, OcrRowCache* rowCache)
{
    std::vector<LootLine> result;

    if (!initialized_ || source.empty())
        return result;

    std::lock_guard lock(mutex_);

    const cv::Rect whole(0, 0, source.cols, source.rows);
    cv::Rect panel = FindPanel(source);

    if (rowCache)
    {
        const std::optional<cv::Rect>& held = rowCache->Panel();

        if (held && ClosePanels(*held, panel) && (*held & whole) == *held)
            panel = *held;

        rowCache->SetPanel(panel);
    }

    const PreparedGray prepared = PrepareGray(source(panel), rowCache ? rowCache->Levels() : std::nullopt);
    const cv::Mat& gray = prepared.gray;

    if (rowCache)
        rowCache->SetLevels(prepared.normalized ? std::optional<TextLevels>(prepared.levels) : std::nullopt);

    ReportPanel(panel, source.size());
    ReportPreparation(prepared.scaled, prepared.normalized, panel.width, gray.cols, prepared.levels.p50, prepared.levels.p95);

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

            if (prepared.scaled || prepared.normalized || panel != whole)
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
        std::filesystem::path debugPath;
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
        job.debugPath = OcrDebugRowPath(debugDir, rowIndex, "read");

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

    const size_t workers = (std::min)(workers_, pending.size());

    std::atomic<bool> rowErrorLogged{ false };

    auto runRow = [this, &rowErrorLogged](RowJob& job)
    {
        try
        {
            job.lines = RecognizeTextOnly(job.textGray, job.debugPath);
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
            runRow(jobs[i]);
    }
    else
    {
        std::atomic<size_t> next{ 0 };
        std::vector<std::jthread> pool;
        pool.reserve(workers);

        for (size_t worker = 0; worker < workers; ++worker)
        {
            pool.emplace_back(
                [&next, &jobs, &pending, &runRow]
                {
                    RunLoggingExceptions(
                        "OCR worker",
                        [&]
                        {
                            for (size_t i = next++; i < pending.size(); i = next++)
                                runRow(jobs[pending[i]]);
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
            line.x1 = panel.x + ToSource(line.x1 + job.offsetX, prepared.scale);
            line.x2 = panel.x + ToSource(line.x2 + job.offsetX, prepared.scale);
            line.y1 = panel.y + ToSource(line.y1 + job.offsetY, prepared.scale);
            line.y2 = panel.y + ToSource(line.y2 + job.offsetY, prepared.scale);

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
