#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
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

    cv::Mat Prepare(const cv::Mat& gray) const;

private:
    struct Layer
    {
        int outputs = 0;
        int inputs = 0;
        int kernelHeight = 0;
        int kernelWidth = 0;
        int padHeight = 0;
        int padWidth = 0;
        int poolHeight = 1;
        int poolWidth = 1;
        bool relu = false;
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

    static bool Fits(const std::vector<Layer>& layers, int inputHeight, int minInputWidth, std::size_t symbols);
    static Tensor Convolve(const Tensor& input, const Layer& layer);
    static Tensor Pool(const Tensor& input, int poolHeight, int poolWidth);

    Tensor InputTensor(const cv::Mat& prepared) const;
    Result Decode(const Tensor& logits, int steps) const;

    int inputHeight_ = 0;
    int stride_ = 1;
    std::vector<std::string> charset_;
    std::vector<Layer> layers_;
};
