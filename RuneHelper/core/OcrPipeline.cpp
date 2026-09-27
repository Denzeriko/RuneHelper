#include "core/OcrPipeline.h"

#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/Logger.h"
#include "ocr/LootRows.h"
#include "price/PriceService.h"

namespace
{
constexpr int kOverlayRowSpacing = 25;

bool HasCloseOverlayText(const std::vector<OverlayText>& texts, int y, int minDistance)
{
    for (const auto& text : texts)
    {
        if (std::abs(text.y - y) < minDistance)
            return true;
    }

    return false;
}
}

OcrPipeline::OcrPipeline(FeatureRegistry& features, PriceService& prices) : features_(features), prices_(prices) {}

bool OcrPipeline::LoadLanguage(const std::string& language)
{
    translations_ = NameMatcher();

    std::string_view model = EmbeddedTextModel(language);

    if (model.empty())
    {
        LOG_ERROR("OCR: this build has no text model for game language '" + language + "', English text is read instead");
        model = EmbeddedTextModel("en");
    }

    if (!ocr_.Init(model))
        return false;

    const bool recipesLoaded = recipes_.Load();

    if (language == "en")
        return true;

    if (recipesLoaded)
        translations_ = recipes_.Translations(language);

    LOG_INFO("OCR: game language '" + language + "', " + std::to_string(translations_.Size()) + " item names to translate");
    return true;
}

std::vector<LootLine> OcrPipeline::RecognizeLoot(const cv::Mat& image, OcrRowCache& rowCache, bool saveDebug)
{
    return ocr_.RecognizeLoot(image, &rowCache, saveDebug);
}

OcrPipelineResult OcrPipeline::BuildFrame(
    const std::vector<LootLine>& loot,
    const cv::Mat& image,
    const cv::Rect& region,
    const AppConfig& config,
    const OcrRowCache& rowCache
)
{
    std::vector<FrameRow> rows = ParseLootRows(loot, region, config, translations_.Empty() ? nullptr : &translations_);
    const bool pricesLoaded = config.priceSearchEnabled && prices_.Status().priceCount > 0;

    for (FrameRow& row : rows)
    {
        if (config.priceSearchEnabled)
            row.price = prices_.Resolve(row.name, row.quantity);

        row.missingPrice =
            pricesLoaded && !row.price.unitEx && recipes_.Loaded() && recipes_.FindRecipe(row.name, row.quantity) != nullptr;
    }

    OcrPipelineResult result;
    result.debug.lines.reserve(loot.size());

    for (const LootLine& item : loot)
    {
        DebugLine line;
        line.ocrText = item.text;
        result.debug.lines.push_back(std::move(line));
    }

    std::vector<RowOverlay> rowOverlays(rows.size());
    FrameContext frame{
        image,
        region,
        rows,
        config,
        prices_.DivineRate(),
        rowOverlays,
        result.overlay,
        result.debug,
        rowCache.Panel().value_or(cv::Rect(0, 0, image.cols, image.rows)),
        rowCache.Levels(),
    };

    features_.RunFrame(frame);

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (rowOverlays[i].note.empty())
            continue;

        const int y = rows[i].overlayY;

        if (HasCloseOverlayText(result.overlay.texts, y, kOverlayRowSpacing))
            continue;

        OverlayText text;
        text.text = rowOverlays[i].note;
        text.color = rowOverlays[i].color;
        text.x = OverlayTextX(region, rowCache.Panel(), config);
        text.y = y;
        result.overlay.texts.push_back(std::move(text));
    }

    return result;
}
