#include "ocr/OcrRowCache.h"

#include <utility>

#include "ocr/OcrFrameDiffer.h"

const OcrRowCache::Row* OcrRowCache::Find(const cv::Mat& textGray)
{
    if (textGray.empty())
    {
        ++misses_;
        return nullptr;
    }

    for (const Row& row : rows_)
    {
        if (row.textGray.size() != textGray.size())
            continue;

        if (!SimilarImages(row.textGray, textGray))
            continue;

        ++hits_;
        return &row;
    }

    ++misses_;
    return nullptr;
}

void OcrRowCache::Store(std::vector<Row> rows)
{
    rows_ = std::move(rows);
}

void OcrRowCache::Reset()
{
    rows_.clear();
    levels_.reset();
    panel_.reset();
    ResetCounters();
}

void OcrRowCache::ResetCounters()
{
    hits_ = 0;
    misses_ = 0;
}
