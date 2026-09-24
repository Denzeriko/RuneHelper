#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <string_view>
#include <vector>

class LineReader
{
public:
    struct Result
    {
        std::string text;
        float confidence = 0.0f;
    };

    bool Load(std::string_view model);

    bool Loaded() const { return !layers_.empty(); }

    Result Read(const cv::Mat& gray) const;

    static cv::Mat Prepare(const cv::Mat& gray);

private:
    struct Layer
    {
        int outputs = 0;
        int inputs = 0;
        int kernelHeight = 0;
        int kernelWidth = 0;
        std::vector<float> weights;
        std::vector<float> bias;
    };

    struct Tensor
    {
        int channels = 0;
        int height = 0;
        int width = 0;
        std::vector<float> data;

        float* Plane(int channel) { return data.data() + static_cast<std::size_t>(channel) * height * width; }

        const float* Plane(int channel) const { return data.data() + static_cast<std::size_t>(channel) * height * width; }
    };

    static Tensor Convolve(const Tensor& input, const Layer& layer, int padHeight, int padWidth, bool relu);
    static Tensor Pool(const Tensor& input, int poolHeight, int poolWidth);

    std::vector<std::string> charset_;
    std::vector<Layer> layers_;
};
