#pragma once

#include <memory>
#include <string>

#include <opencv2/core.hpp>

class TextRaster
{
public:
    static TextRaster& Instance();

    bool Ready() const;

    cv::Size Measure(const std::string& utf8, int pixelHeight);
    int Descent(int pixelHeight);

    void Draw(
        cv::Mat& canvas,
        const std::string& utf8,
        const cv::Point& baseline,
        int pixelHeight,
        const cv::Scalar& color,
        bool outline);

private:
    TextRaster();
    ~TextRaster();

    TextRaster(const TextRaster&) = delete;
    TextRaster& operator=(const TextRaster&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
