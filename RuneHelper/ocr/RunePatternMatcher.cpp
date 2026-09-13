#include "ocr/RunePatternMatcher.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "core/Logger.h"
#include "platform/PlatformPaths.h"

namespace
{
constexpr double kNmsIouThreshold = 0.35;
constexpr int kCalibrationTotal = 21;
constexpr int kCalibrationMaxAttempts = 2;
constexpr double kCalibrationScaleMin = 0.60;
constexpr double kCalibrationScaleStep = 0.05;

double CalibrationScale(int index)
{
    return kCalibrationScaleMin + index * kCalibrationScaleStep;
}

bool IsImageFile(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();

    for (char& ch : ext)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
}

std::string NormalizeTemplateName(const std::filesystem::path& path)
{
    std::string name = path.stem().string();

    for (char& ch : name)
    {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch))
            ch = static_cast<char>(std::tolower(uch));
        else
            ch = '_';
    }

    return name;
}

std::string RankLabelFromTemplateName(const std::string& name)
{
    if (name.empty())
        return "A";

    const char rank = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
    if (rank != 'S' && rank != 'A' && rank != 'B' && rank != 'C' && rank != 'D')
        return "A";

    if (name.size() == 1 || name[1] == '_' || name[1] == '-' || std::isdigit(static_cast<unsigned char>(name[1])))
        return std::string(1, rank);

    return "A";
}

void SuppressAround(cv::Mat& result, cv::Point loc, cv::Size templateSize)
{
    cv::Rect suppress(
        std::max(0, loc.x - templateSize.width / 2),
        std::max(0, loc.y - templateSize.height / 2),
        templateSize.width * 2,
        templateSize.height * 2
    );

    suppress &= cv::Rect(0, 0, result.cols, result.rows);
    if (suppress.width > 0 && suppress.height > 0)
        result(suppress).setTo(0.0f);
}

double RectIou(const cv::Rect& a, const cv::Rect& b)
{
    const int intersection = (a & b).area();
    const int unionArea = a.area() + b.area() - intersection;

    if (unionArea <= 0)
        return 0.0;

    return static_cast<double>(intersection) / static_cast<double>(unionArea);
}

std::vector<RunePatternMatch> NonMaxSuppress(std::vector<RunePatternMatch> matches)
{
    std::sort(
        matches.begin(),
        matches.end(),
        [](const RunePatternMatch& a, const RunePatternMatch& b)
        {
            return a.score > b.score;
        });

    std::vector<RunePatternMatch> filtered;
    filtered.reserve(matches.size());

    for (const auto& match : matches)
    {
        bool duplicate = false;

        for (const auto& kept : filtered)
        {
            if (match.name == kept.name && RectIou(match.rect, kept.rect) > kNmsIouThreshold)
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            filtered.push_back(match);
    }

    return filtered;
}

cv::Mat ScaleTemplate(const cv::Mat& templ, double scale)
{
    if (scale == 1.0)
        return templ;

    cv::Mat scaledTemplate;
    cv::resize(
        templ,
        scaledTemplate,
        cv::Size(),
        scale,
        scale,
        scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR
    );

    return scaledTemplate;
}

}


void RunePatternMatcher::EnsureTemplates()
{
    if (templatesLoaded_)
        return;

    templatesLoaded_ = true;

    const std::filesystem::path dir = GetUserDataDir() / "runes";
    LOG_INFO("Rune pattern template dir: " + dir.string());

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
    {
        LOG_INFO("Rune pattern templates loaded: 0");
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (ec || !entry.is_regular_file() || !IsImageFile(entry.path()))
            continue;

        cv::Mat image = cv::imread(entry.path().string(), cv::IMREAD_COLOR);
        if (image.empty())
            continue;

        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

        std::string name = NormalizeTemplateName(entry.path());
        if (name.empty())
            continue;

        std::string label = RankLabelFromTemplateName(name);

        LOG_INFO("Rune pattern template loaded: " + name + " rank=" + label + " from " + entry.path().string());
        templates_.push_back({ std::move(name), std::move(label), std::move(gray) });
    }

    LOG_INFO("Rune pattern templates loaded: " + std::to_string(templates_.size()));
}

const std::vector<cv::Mat>& RunePatternMatcher::ScaledTemplates(double scale)
{
    if (scaledTemplatesScale_ == scale && scaledTemplates_.size() == templates_.size())
        return scaledTemplates_;

    scaledTemplates_.clear();
    scaledTemplates_.reserve(templates_.size());

    for (const auto& templ : templates_)
        scaledTemplates_.push_back(templ.gray.empty() ? cv::Mat() : ScaleTemplate(templ.gray, scale));

    scaledTemplatesScale_ = scale;

    return scaledTemplates_;
}

std::vector<RunePatternMatch> RunePatternMatcher::FindAtScale(
    const cv::Mat& sourceGray,
    double threshold,
    double scale)
{
    std::vector<RunePatternMatch> matches;

    const std::vector<cv::Mat>& scaledTemplates = ScaledTemplates(scale);

    for (std::size_t index = 0; index < templates_.size(); ++index)
    {
        const Template& templ = templates_[index];
        const cv::Mat& scaledTemplate = scaledTemplates[index];

        if (scaledTemplate.empty() ||
            scaledTemplate.cols < 8 ||
            scaledTemplate.rows < 8 ||
            scaledTemplate.cols > sourceGray.cols ||
            scaledTemplate.rows > sourceGray.rows)
        {
            continue;
        }

        cv::Mat result;
        cv::matchTemplate(sourceGray, scaledTemplate, result, cv::TM_CCOEFF_NORMED);

        while (true)
        {
            double minVal = 0.0;
            double maxVal = 0.0;
            cv::Point minLoc;
            cv::Point maxLoc;

            cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

            if (maxVal < threshold)
                break;

            matches.push_back({
                templ.name,
                templ.label,
                cv::Rect(maxLoc.x, maxLoc.y, scaledTemplate.cols, scaledTemplate.rows),
                maxVal,
                scale
            });

            SuppressAround(result, maxLoc, scaledTemplate.size());
        }
    }

    return NonMaxSuppress(std::move(matches));
}

double RunePatternMatcher::CurrentSearchScale() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (calibratedScale_ > 0.0)
        return calibratedScale_;

    return 1.0;
}

void RunePatternMatcher::Calibration::Restart()
{
    running = true;
    index = 0;
    currentScale = CalibrationScale(0);
    bestScale = 0.0;
    bestMatches = 0;
    bestScoreSum = -1.0;
}

void RunePatternMatcher::BeginScaleCalibration()
{
    std::lock_guard<std::mutex> lock(mutex_);

    calibration_ = Calibration();
    calibration_.Restart();

    LOG_INFO("Rune pattern scale calibration requested");
}

RunePatternCalibrationStatus RunePatternMatcher::CalibrationStatus() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    RunePatternCalibrationStatus status;
    status.running = calibration_.running;
    status.step = calibration_.index;
    status.total = kCalibrationTotal;
    status.currentScale = calibration_.currentScale;
    status.bestScale = calibration_.bestScale;
    status.bestMatches = calibration_.bestMatches;

    if (!status.running && status.bestScale <= 0.0)
        status.bestScale = calibratedScale_;

    return status;
}

void RunePatternMatcher::SetSearchScale(double scale)
{
    std::lock_guard<std::mutex> lock(mutex_);
    calibratedScale_ = scale > 0.0 ? scale : 0.0;
}

void RunePatternMatcher::StopCalibration(const char* reason)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!calibration_.running)
        return;

    calibration_ = Calibration();

    LOG_ERROR(reason);
}

bool RunePatternMatcher::TakeNextCalibrationScale(double& scale)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!calibration_.running)
        return false;

    if (calibration_.index < 0 || calibration_.index >= kCalibrationTotal)
        calibration_.index = 0;

    calibration_.currentScale = CalibrationScale(calibration_.index);
    scale = calibration_.currentScale;

    return true;
}

void RunePatternMatcher::AdvanceCalibration(double scale, std::size_t matches, double scoreSum)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!calibration_.running)
        return;

    if (matches > calibration_.bestMatches ||
        (matches == calibration_.bestMatches && scoreSum > calibration_.bestScoreSum))
    {
        calibration_.bestScale = scale;
        calibration_.bestMatches = matches;
        calibration_.bestScoreSum = scoreSum;
    }

    ++calibration_.index;

    if (calibration_.index < kCalibrationTotal)
    {
        calibration_.currentScale = CalibrationScale(calibration_.index);
        return;
    }

    if (calibration_.bestMatches == 0)
    {
        ++calibration_.attempts;

        if (calibration_.attempts >= kCalibrationMaxAttempts)
        {
            calibration_ = Calibration();

            LOG_ERROR(
                "Rune pattern scale calibration gave up after " +
                std::to_string(kCalibrationMaxAttempts) +
                " sweeps without a single match"
            );

            return;
        }

        LOG_INFO("Rune pattern scale calibration found no matches; restarting scale matching");
        calibration_.Restart();

        return;
    }

    calibratedScale_ = calibration_.bestScale;
    calibration_.running = false;
    calibration_.currentScale = calibration_.bestScale;

    LOG_INFO(
        "Rune pattern scale calibrated: scale=" + std::to_string(calibration_.bestScale) +
        " matches=" + std::to_string(calibration_.bestMatches)
    );
}

void RunePatternMatcher::StepScaleCalibration(const cv::Mat& sourceGray, double threshold)
{
    EnsureTemplates();

    if (sourceGray.empty())
        return;

    if (templates_.empty())
    {
        StopCalibration("Rune pattern scale calibration stopped: no rune templates loaded");
        return;
    }

    double scale = 0.0;

    if (!TakeNextCalibrationScale(scale))
        return;

    const std::vector<RunePatternMatch> matches = FindAtScale(sourceGray, threshold, scale);

    double scoreSum = 0.0;

    for (const auto& match : matches)
        scoreSum += match.score;

    LOG_INFO(
        "Rune pattern scale calibration: scale=" + std::to_string(scale) +
        " matches=" + std::to_string(matches.size()) +
        " scoreSum=" + std::to_string(scoreSum)
    );

    AdvanceCalibration(scale, matches.size(), scoreSum);
}

std::vector<RunePatternMatch> RunePatternMatcher::Find(const cv::Mat& sourceGray, double threshold)
{
    std::vector<RunePatternMatch> matches;

    EnsureTemplates();

    if (sourceGray.empty() || templates_.empty())
        return matches;

    const double scale = CurrentSearchScale();
    matches = FindAtScale(sourceGray, threshold, scale);

    std::sort(
        matches.begin(),
        matches.end(),
        [](const RunePatternMatch& a, const RunePatternMatch& b)
        {
            if (a.rect.y != b.rect.y)
                return a.rect.y < b.rect.y;

            return a.rect.x < b.rect.x;
        });

    return matches;
}
