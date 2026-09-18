#pragma once

#include "core/Feature.h"

class PriceOverlayFeature : public Feature
{
public:
    std::string Name() const override { return "prices"; }

    void OnFrame(FrameContext& frame) override;
};
