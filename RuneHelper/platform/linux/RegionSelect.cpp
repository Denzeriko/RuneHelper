#include "RegionSelect.h"

#include <stdexcept>

RegionSelector::~RegionSelector() = default;

cv::Rect RegionSelector::Select()
{
    throw std::runtime_error("Linux region selection is not implemented");
}
