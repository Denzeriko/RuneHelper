#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <opencv2/core.hpp>

#include "ocr/OCR.h"

class OcrRowCache
{
public:
    struct Row
    {
        cv::Mat textGray;
        std::optional<LootLine> line;
    };

    const Row* Find(const cv::Mat& textGray);
    void Store(std::vector<Row> rows);
    void Reset();

    const std::optional<TextLevels>& Levels() const { return levels_; }

    void SetLevels(const std::optional<TextLevels>& levels) { levels_ = levels; }

    const std::optional<cv::Rect>& Panel() const { return panel_; }

    void SetPanel(const cv::Rect& panel) { panel_ = panel; }

    std::size_t Hits() const { return hits_; }

    std::size_t Misses() const { return misses_; }

    void ResetCounters();

private:
    std::vector<Row> rows_;
    std::optional<TextLevels> levels_;
    std::optional<cv::Rect> panel_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};
