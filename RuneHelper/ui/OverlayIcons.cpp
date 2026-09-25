#include "ui/OverlayIcons.h"

#include <string>
#include <string_view>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "core/Logger.h"
#include "core/Text.h"

#ifdef _WIN32
#include "platform/windows/ResourceHelper.h"
#else
#include "platform/linux/ResourceHelper.h"
#endif

const OverlayIcon* FindOverlayIcon(char32_t codePoint)
{
    for (const OverlayIcon& icon : kOverlayIcons)
    {
        if (icon.codePoint == codePoint)
            return &icon;
    }

    return nullptr;
}

std::string IconString(const OverlayIcon& icon)
{
    std::string text;
    AppendUtf8(text, icon.codePoint);

    return text;
}

std::string WithIconLabels(std::string_view utf8)
{
    std::string text;

    for (const char32_t codePoint : DecodeUtf8(utf8))
    {
        if (const OverlayIcon* icon = FindOverlayIcon(codePoint))
            text += icon->label;
        else
            AppendUtf8(text, codePoint);
    }

    return text;
}

cv::Mat LoadIconImage(const OverlayIcon& icon)
{
    const std::string_view bytes = EmbeddedImage(icon.file);

    if (bytes.empty())
    {
        LOG_ERROR("Overlay icon is not embedded in the binary: " + std::string(icon.file));
        return {};
    }

    const cv::Mat encoded(1, static_cast<int>(bytes.size()), CV_8UC1, const_cast<char*>(bytes.data()));
    cv::Mat image = cv::imdecode(encoded, cv::IMREAD_UNCHANGED);

    if (image.empty())
    {
        LOG_ERROR("Overlay icon could not be decoded: " + std::string(icon.file));
        return {};
    }

    if (image.depth() == CV_16U)
        image.convertTo(image, CV_8U, 1.0 / 257.0);

    if (image.type() == CV_8UC1)
        cv::cvtColor(image, image, cv::COLOR_GRAY2BGRA);
    else if (image.type() == CV_8UC3)
        cv::cvtColor(image, image, cv::COLOR_BGR2BGRA);

    if (image.type() != CV_8UC4)
    {
        LOG_ERROR("Overlay icon has an unsupported pixel format: " + std::string(icon.file));
        return {};
    }

    return image;
}
