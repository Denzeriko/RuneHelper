#include "core/Feature.h"

#include "core/ExceptionLogging.h"
#include "core/Logger.h"

void FeatureRegistry::Add(std::unique_ptr<Feature> feature)
{
    features_.push_back(std::move(feature));
}

bool FeatureRegistry::InitAll(ConfigManager& configManager)
{
    bool ok = true;

    for (const auto& feature : features_)
    {
        const std::string context = "Feature " + feature->Name() + " init";
        bool initialised = false;

        RunLoggingExceptions(context.c_str(), [&] { initialised = feature->Init(configManager); });

        if (initialised)
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
