#include "OCR.h"

#include "core/Logger.h"
#include "core/ThreadGuard.h"
#include "ocr/OcrDebug.h"
#include "ocr/OcrRowCache.h"
#include "ocr/RowFinder.h"
#include "ocr/TextStart.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <exception>
#include <optional>
#include <thread>

namespace
{
constexpr std::size_t kMaxOcrWorkers = 8;

constexpr float kMinReadConfidence = 80.0f;

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

cv::Rect ChoosePanel(const cv::Mat& source, OcrRowCache* rowCache)
{
    const cv::Rect whole(0, 0, source.cols, source.rows);
    cv::Rect panel = FindPanel(source);

    if (rowCache)
    {
        const std::optional<cv::Rect>& held = rowCache->Panel();

        if (held && ClosePanels(*held, panel) && (*held & whole) == *held)
            panel = *held;

        rowCache->SetPanel(panel);
    }

    return panel;
}

std::vector<RowJob> CutRows(
    const cv::Mat& gray,
    const std::vector<cv::Rect>& rows,
    const std::vector<int>& textStarts,
    OcrRowCache* rowCache,
    OcrDebugDump* debug
)
{
    std::vector<RowJob> jobs(rows.size());

    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
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

        if (debug)
            job.debugPath = debug->SaveRow(rowIndex, rowGray, job.textGray, rowRect, job.offsetX);
    }

    return jobs;
}

template <typename ReadRow>
void ReadPendingRows(std::vector<RowJob>& jobs, std::size_t maxWorkers, ReadRow readRow)
{
    std::vector<std::size_t> pending;
    pending.reserve(jobs.size());

    for (std::size_t i = 0; i < jobs.size(); ++i)
    {
        if (!jobs[i].reused)
            pending.push_back(i);
    }

    const std::size_t workers = (std::min)(maxWorkers, pending.size());

    if (workers <= 1)
    {
        for (std::size_t i : pending)
            readRow(jobs[i]);

        return;
    }

    std::atomic<std::size_t> next{ 0 };
    std::vector<std::jthread> pool;
    pool.reserve(workers);

    for (std::size_t worker = 0; worker < workers; ++worker)
    {
        pool.emplace_back(
            [&next, &jobs, &pending, &readRow]
            {
                RunLoggingExceptions(
                    "OCR worker",
                    [&]
                    {
                        for (std::size_t i = next++; i < pending.size(); i = next++)
                            readRow(jobs[pending[i]]);
                    }
                );
            }
        );
    }
}

void StoreRows(OcrRowCache& rowCache, const std::vector<RowJob>& jobs)
{
    std::vector<OcrRowCache::Row> generation;
    generation.reserve(jobs.size());

    for (const RowJob& job : jobs)
        generation.push_back({ job.reused ? job.anchor : job.textGray.clone(), job.lines });

    rowCache.Store(std::move(generation));
}

std::vector<LootLine> ToSourceLines(std::vector<RowJob>& jobs, const cv::Rect& panel, double scale)
{
    std::vector<LootLine> result;

    for (RowJob& job : jobs)
    {
        for (LootLine& line : job.lines)
        {
            line.x1 = panel.x + ToSource(line.x1 + job.offsetX, scale);
            line.x2 = panel.x + ToSource(line.x2 + job.offsetX, scale);
            line.y1 = panel.y + ToSource(line.y1 + job.offsetY, scale);
            line.y2 = panel.y + ToSource(line.y2 + job.offsetY, scale);

            result.push_back(std::move(line));
        }
    }

    return result;
}
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

std::vector<LootLine> OCR::RecognizeLoot(const cv::Mat& source, OcrRowCache* rowCache, bool saveDebug)
{
    if (!initialized_ || source.empty())
        return {};

    std::lock_guard lock(mutex_);

    const cv::Rect whole(0, 0, source.cols, source.rows);
    const cv::Rect panel = ChoosePanel(source, rowCache);
    const PreparedGray prepared = PrepareGray(source(panel), rowCache ? rowCache->Levels() : std::nullopt);
    const cv::Mat& gray = prepared.gray;

    if (rowCache)
        rowCache->SetLevels(prepared.normalized ? std::optional<TextLevels>(prepared.levels) : std::nullopt);

    ReportPanel(panel, source.size());
    ReportPreparation(prepared.scaled, prepared.normalized, panel.width, gray.cols, prepared.levels.p50, prepared.levels.p95);

    OcrDebugDump debug;
    const bool debugOCR = saveDebug && debug.Start(source, gray, prepared.scaled || prepared.normalized || panel != whole);

    const std::vector<cv::Rect> rows = FindLootRows(gray);
    const std::vector<int> textStarts = FindTextStartX(gray, rows);

    if (debugOCR)
        rowCache = nullptr;

    std::vector<RowJob> jobs = CutRows(gray, rows, textStarts, rowCache, debugOCR ? &debug : nullptr);
    std::atomic<bool> rowErrorLogged{ false };

    ReadPendingRows(
        jobs,
        workers_,
        [this, &rowErrorLogged](RowJob& job)
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
        }
    );

    if (rowCache)
        StoreRows(*rowCache, jobs);

    std::vector<LootLine> result = ToSourceLines(jobs, panel, prepared.scale);

    if (debugOCR)
        debug.Finish();

    return result;
}

void OCR::Trim(std::string& s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));

    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
}
