#include "RegionSelect.h"

#include "core/Logger.h"

RegionSelector::~RegionSelector() = default;

cv::Rect RegionSelector::Select()
{
    LOG_ERROR("Linux region selection is not implemented");
    return {};
}
