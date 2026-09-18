#include "core/Feature.h"

#include "core/Logger.h"

void FeatureRegistry::Add(std::unique_ptr<Feature> feature)
{
    if (feature)
        features_.push_back(std::move(feature));
}

bool FeatureRegistry::InitAll(ConfigManager& configManager)
{
    bool ok = true;

    for (const auto& feature : features_)
    {
        if (feature->Init(configManager))
            continue;

        LOG_ERROR("Feature failed to initialise: " + feature->Name());
        ok = false;
    }

    return ok;
}

void FeatureRegistry::ShutdownAll()
{
    for (auto it = features_.rbegin(); it != features_.rend(); ++it)
        (*it)->Shutdown();
}

void FeatureRegistry::NotifyRegionChanged()
{
    for (const auto& feature : features_)
        feature->OnRegionChanged();
}

void FeatureRegistry::RunFrame(FrameContext& frame)
{
    for (const auto& feature : features_)
        feature->OnFrame(frame);
}
