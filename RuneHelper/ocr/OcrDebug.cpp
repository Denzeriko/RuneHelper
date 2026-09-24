#include "ocr/OcrDebug.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "core/Logger.h"
#include "platform/PlatformPaths.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace
{
std::filesystem::path PrepareOcrDebugDir()
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
}

bool SaveOcrDebugImage(const std::filesystem::path& path, const cv::Mat& img)
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

void SaveOcrDebugText(
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

bool OcrDebugDump::Start(const cv::Mat& source, const cv::Mat& gray, bool savePrepared)
{
    dir_ = PrepareOcrDebugDir();

    if (dir_.empty())
        return false;

    SaveOcrDebugImage(dir_ / "source.png", source);

    if (savePrepared)
        SaveOcrDebugImage(dir_ / "prepared.png", gray);

    cv::cvtColor(gray, rows_, cv::COLOR_GRAY2BGR);

    return true;
}

std::filesystem::path OcrDebugDump::SaveRow(
    std::size_t index,
    const cv::Mat& rowGray,
    const cv::Mat& textGray,
    const cv::Rect& rowRect,
    int textX
)
{
    SaveOcrDebugImage(RowPath(index, "row"), rowGray);
    SaveOcrDebugImage(RowPath(index, "text"), textGray);

    cv::rectangle(rows_, rowRect, cv::Scalar(0, 255, 0), 2);
    cv::line(rows_, cv::Point(textX, rowRect.y), cv::Point(textX, rowRect.y + rowRect.height), cv::Scalar(255, 0, 0), 2);

    return RowPath(index, "read");
}

void OcrDebugDump::Finish() const
{
    SaveOcrDebugImage(dir_ / "rows_detected.png", rows_);
}

std::filesystem::path OcrDebugDump::RowPath(std::size_t index, const char* suffix) const
{
    std::ostringstream name;
    name << "row_" << std::setw(2) << std::setfill('0') << index << "_" << suffix << ".png";
    return dir_ / name.str();
}
