#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

enum class Surroundings
{
    None,
    Dark,
    Bright,
    Busy
};

inline cv::Point SurroundingOffset(const cv::Mat& gray, double margin = 1.0)
{
    return cv::Point(static_cast<int>(gray.cols * 0.2 * margin), static_cast<int>(gray.rows * 0.1 * margin));
}

inline cv::Mat SurroundWithGame(const cv::Mat& gray, Surroundings surroundings, double margin = 1.0)
{
    if (surroundings == Surroundings::None)
        return gray;

    const cv::Point offset = SurroundingOffset(gray, margin);

    cv::Mat framed(gray.rows + 2 * offset.y, gray.cols + 2 * offset.x, CV_8UC1);
    cv::RNG rng(12345);

    switch (surroundings)
    {
    case Surroundings::Dark:
        rng.fill(framed, cv::RNG::UNIFORM, 15, 70);
        cv::GaussianBlur(framed, framed, cv::Size(9, 9), 3);
        break;

    case Surroundings::Bright:
        rng.fill(framed, cv::RNG::UNIFORM, 150, 240);
        cv::GaussianBlur(framed, framed, cv::Size(9, 9), 3);
        break;

    case Surroundings::Busy:
    {
        cv::Mat coarse(framed.rows / 24 + 1, framed.cols / 24 + 1, CV_8UC1);
        rng.fill(coarse, cv::RNG::UNIFORM, 10, 230);
        cv::resize(coarse, framed, framed.size(), 0, 0, cv::INTER_CUBIC);

        cv::Mat grain(framed.size(), CV_8UC1);
        rng.fill(grain, cv::RNG::UNIFORM, 0, 30);
        framed += grain;
        break;
    }

    case Surroundings::None: break;
    }

    gray.copyTo(framed(cv::Rect(offset.x, offset.y, gray.cols, gray.rows)));

    return framed;
}
