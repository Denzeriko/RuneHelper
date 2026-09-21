#pragma once

#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

#include "ocr/OCR.h"

class OcrRowCache
{
public:
    struct Row
    {
        cv::Mat textGray;
        std::vector<LootLine> lines;
    };

    const Row* Find(const cv::Mat& textGray);
    void Store(std::vector<Row> rows);
    void Reset();

    std::size_t Hits() const { return hits_; }
    std::size_t Misses() const { return misses_; }
    void ResetCounters();

private:
    std::vector<Row> rows_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};
