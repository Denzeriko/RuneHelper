#include "ocr/RunePatternMatcher.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "core/Logger.h"
#include "platform/PlatformPaths.h"

namespace
{
constexpr double kNmsIouThreshold = 0.35;
constexpr double kCalibrationScales[] = {
    0.60,
    0.65,
    0.70,
    0.75,
    0.80,
    0.85,
    0.90,
    0.95,
    1.00, //2560x1440
    1.05,
    1.10,
    1.15,
    1.20,
    1.25,
    1.30,
    1.35,
    1.40,
    1.45,
    1.50, //3840x2160
    1.55,
    1.60
};
constexpr int kCalibrationTotal = static_cast<int>(std::size(kCalibrationScales));

struct RunePatternTemplate
{
    std::string name;
    std::string label;
    cv::Mat gray;
};

std::once_flag g_loadTemplatesOnce;
std::vector<RunePatternTemplate> g_templates;
std::vector<cv::Mat> g_scaledTemplates;
double g_scaledTemplatesScale = -1.0;
std::mutex g_scaleMutex;
double g_calibratedScale = 0.0;

std::mutex g_calibrationMutex;
bool g_calibrationRunning = false;
int g_calibrationIndex = 0;
double g_calibrationCurrentScale = kCalibrationScales[0];
double g_calibrationBestScale = 0.0;
size_t g_calibrationBestMatches = 0;
double g_calibrationBestScoreSum = -1.0;

bool IsImageFile(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(
        ext.begin(),
        ext.end(),
        ext.begin(),
        [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });

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

void LoadTemplates()
{
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
        g_templates.push_back({ std::move(name), std::move(label), std::move(gray) });
    }

    LOG_INFO("Rune pattern templates loaded: " + std::to_string(g_templates.size()));
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

const std::vector<cv::Mat>& ScaledTemplates(double scale)
{
    if (g_scaledTemplatesScale == scale && g_scaledTemplates.size() == g_templates.size())
        return g_scaledTemplates;

    g_scaledTemplates.clear();
    g_scaledTemplates.reserve(g_templates.size());

    for (const auto& templ : g_templates)
        g_scaledTemplates.push_back(templ.gray.empty() ? cv::Mat() : ScaleTemplate(templ.gray, scale));

    g_scaledTemplatesScale = scale;

    return g_scaledTemplates;
}

std::vector<RunePatternMatch> FindMatchesAtScale(
    const cv::Mat& sourceGray,
    double threshold,
    double scale)
{
    std::vector<RunePatternMatch> matches;

    const std::vector<cv::Mat>& scaledTemplates = ScaledTemplates(scale);

    for (std::size_t index = 0; index < g_templates.size(); ++index)
    {
        const RunePatternTemplate& templ = g_templates[index];
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

double CurrentSearchScale()
{
    std::lock_guard lock(g_scaleMutex);

    if (g_calibratedScale > 0.0)
        return g_calibratedScale;

    return 1.0;
}
}

void BeginRunePatternScaleCalibration()
{
    std::call_once(g_loadTemplatesOnce, LoadTemplates);

    std::lock_guard lock(g_calibrationMutex);
    g_calibrationRunning = true;
    g_calibrationIndex = 0;
    g_calibrationCurrentScale = kCalibrationScales[0];
    g_calibrationBestScale = 0.0;
    g_calibrationBestMatches = 0;
    g_calibrationBestScoreSum = -1.0;

    LOG_INFO("Rune pattern scale calibration requested");
}

RunePatternCalibrationStatus GetRunePatternCalibrationStatus()
{
    RunePatternCalibrationStatus status;

    {
        std::lock_guard lock(g_calibrationMutex);
        status.running = g_calibrationRunning;
        status.step = g_calibrationIndex;
        status.total = kCalibrationTotal;
        status.currentScale = g_calibrationCurrentScale;
        status.bestScale = g_calibrationBestScale;
        status.bestMatches = g_calibrationBestMatches;
    }

    if (!status.running && status.bestScale <= 0.0)
    {
        std::lock_guard lock(g_scaleMutex);
        status.bestScale = g_calibratedScale;
    }

    return status;
}

void SetRunePatternSearchScale(double scale)
{
    std::lock_guard lock(g_scaleMutex);
    g_calibratedScale = scale > 0.0 ? scale : 0.0;
}

void StepRunePatternScaleCalibration(const cv::Mat& sourceGray, double threshold)
{
    std::call_once(g_loadTemplatesOnce, LoadTemplates);

    if (sourceGray.empty())
        return;

    if (g_templates.empty())
    {
        std::lock_guard lock(g_calibrationMutex);
        if (g_calibrationRunning)
        {
            g_calibrationRunning = false;
            g_calibrationBestScale = 0.0;
            g_calibrationBestMatches = 0;
            LOG_ERROR("Rune pattern scale calibration stopped: no rune templates loaded");
        }
        return;
    }

    {
        std::lock_guard lock(g_calibrationMutex);
        if (!g_calibrationRunning)
            return;

        if (g_calibrationIndex < 0 || g_calibrationIndex >= kCalibrationTotal)
            g_calibrationIndex = 0;

        g_calibrationCurrentScale = kCalibrationScales[g_calibrationIndex];
    }

    double scale = 1.0;
    {
        std::lock_guard lock(g_calibrationMutex);
        scale = g_calibrationCurrentScale;
    }

    auto matches = FindMatchesAtScale(sourceGray, threshold, scale);

    double scoreSum = 0.0;
    for (const auto& match : matches)
        scoreSum += match.score;

    LOG_INFO(
        "Rune pattern scale calibration: scale=" +
        std::to_string(scale) +
        " matches=" +
        std::to_string(matches.size()) +
        " scoreSum=" +
        std::to_string(scoreSum)
    );

    std::lock_guard lock(g_calibrationMutex);
    if (!g_calibrationRunning)
        return;

    if (matches.size() > g_calibrationBestMatches ||
        (matches.size() == g_calibrationBestMatches && scoreSum > g_calibrationBestScoreSum))
    {
        g_calibrationBestScale = scale;
        g_calibrationBestMatches = matches.size();
        g_calibrationBestScoreSum = scoreSum;
    }

    ++g_calibrationIndex;

    if (g_calibrationIndex < kCalibrationTotal)
    {
        g_calibrationCurrentScale = kCalibrationScales[g_calibrationIndex];
        return;
    }

    if (g_calibrationBestMatches == 0)
    {
        LOG_INFO("Rune pattern scale calibration found no matches; restarting scale matching");
        g_calibrationIndex = 0;
        g_calibrationCurrentScale = kCalibrationScales[0];
        g_calibrationBestScale = 0.0;
        g_calibrationBestMatches = 0;
        g_calibrationBestScoreSum = -1.0;
        return;
    }

    {
        std::lock_guard scaleLock(g_scaleMutex);
        g_calibratedScale = g_calibrationBestScale;
    }

    g_calibrationRunning = false;
    g_calibrationCurrentScale = g_calibrationBestScale;

    LOG_INFO(
        "Rune pattern scale calibrated: scale=" +
        std::to_string(g_calibrationBestScale) +
        " matches=" +
        std::to_string(g_calibrationBestMatches)
    );
}

std::vector<RunePatternMatch> FindRunePatternMatches(const cv::Mat& sourceGray, double threshold)
{
    std::vector<RunePatternMatch> matches;

    std::call_once(g_loadTemplatesOnce, LoadTemplates);

    if (sourceGray.empty() || g_templates.empty())
        return matches;

    const double scale = CurrentSearchScale();
    matches = FindMatchesAtScale(sourceGray, threshold, scale);

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
