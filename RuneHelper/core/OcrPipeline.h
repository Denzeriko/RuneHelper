#pragma once

#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "core/DebugData.h"
#include "core/Feature.h"
#include "core/Config.h"
#include "ocr/OCR.h"
#include "ocr/OcrRowCache.h"
#include "recipes/RecipeDatabase.h"
#include "ui/OverlayState.h"

class PriceService;

struct OcrPipelineResult
{
    OverlayFrame overlay;
    DebugData debug;
};

class OcrPipeline
{
public:
    OcrPipeline(FeatureRegistry& features, PriceService& prices);

    bool LoadLanguage(const std::string& language);
    std::vector<LootLine> RecognizeLoot(const cv::Mat& image, OcrRowCache& rowCache, bool saveDebug);
    OcrPipelineResult BuildFrame(
        const std::vector<LootLine>& loot,
        const cv::Mat& image,
        const cv::Rect& region,
        const AppConfig& config,
        const OcrRowCache& rowCache
    );

private:
    OCR ocr_;
    NameMatcher translations_;
    RecipeDatabase recipes_;
    FeatureRegistry& features_;
    PriceService& prices_;
};
