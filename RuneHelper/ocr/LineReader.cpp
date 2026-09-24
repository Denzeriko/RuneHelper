#include "LineReader.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
constexpr int kInputHeight = 24;
constexpr int kStride = 4;
constexpr std::size_t kLayerCount = 8;
constexpr std::uint32_t kMaxSymbols = 65536;
constexpr char kMagic[8] = { 'R', 'H', 'O', 'C', 'R', '2', '\0', '\0' };

class Cursor
{
public:
    explicit Cursor(std::string_view data) : data_(data) {}

    bool Read(void* target, std::size_t size)
    {
        if (offset_ + size > data_.size())
            return false;

        std::memcpy(target, data_.data() + offset_, size);
        offset_ += size;
        return true;
    }

    bool ReadU32(std::uint32_t& value) { return Read(&value, sizeof(value)); }

    bool ReadU8(std::uint8_t& value) { return Read(&value, sizeof(value)); }

private:
    std::string_view data_;
    std::size_t offset_ = 0;
};

float Percentile(std::vector<float>& values, double share)
{
    const double position = share * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = std::min(values.size() - 1, lower + 1);

    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lower), values.end());
    const float low = values[lower];

    if (upper == lower)
        return low;

    const float high = *std::min_element(values.begin() + static_cast<std::ptrdiff_t>(lower) + 1, values.end());

    return static_cast<float>(low + (high - low) * (position - static_cast<double>(lower)));
}
}

bool LineReader::Load(std::string_view model)
{
    layers_.clear();
    charset_.clear();

    Cursor cursor(model);
    char magic[8] = {};
    std::uint32_t charsetSize = 0;

    if (!cursor.Read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(magic)) != 0 || !cursor.ReadU32(charsetSize) ||
        charsetSize == 0 || charsetSize > kMaxSymbols)
    {
        return false;
    }

    std::vector<std::string> charset(charsetSize);

    for (std::string& symbol : charset)
    {
        std::uint8_t length = 0;

        if (!cursor.ReadU8(length) || length == 0)
            return false;

        symbol.resize(length);

        if (!cursor.Read(symbol.data(), length))
            return false;
    }

    std::uint32_t layerCount = 0;

    if (!cursor.ReadU32(layerCount) || layerCount != kLayerCount)
        return false;

    std::vector<Layer> layers(layerCount);

    for (Layer& layer : layers)
    {
        std::uint32_t dims[4] = {};

        if (!cursor.Read(dims, sizeof(dims)))
            return false;

        layer.outputs = static_cast<int>(dims[0]);
        layer.inputs = static_cast<int>(dims[1]);
        layer.kernelHeight = static_cast<int>(dims[2]);
        layer.kernelWidth = static_cast<int>(dims[3]);
        layer.weights.resize(static_cast<std::size_t>(dims[0]) * dims[1] * dims[2] * dims[3]);
        layer.bias.resize(dims[0]);

        if (!cursor.Read(layer.weights.data(), layer.weights.size() * sizeof(float)) ||
            !cursor.Read(layer.bias.data(), layer.bias.size() * sizeof(float)))
        {
            return false;
        }
    }

    for (std::size_t i = 1; i < layers.size(); ++i)
    {
        if (layers[i].inputs != layers[i - 1].outputs)
            return false;
    }

    if (layers.front().inputs != 1 || layers.back().outputs != static_cast<int>(charset.size()) + 1)
        return false;

    charset_ = std::move(charset);
    layers_ = std::move(layers);
    return true;
}

cv::Mat LineReader::Prepare(const cv::Mat& gray)
{
    if (gray.empty() || gray.type() != CV_8UC1)
        return {};

    const double scale = static_cast<double>(kInputHeight) / gray.rows;
    const int width = std::max(8, static_cast<int>(std::nearbyint(gray.cols * scale)));

    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(width, kInputHeight), 0, 0, scale < 1.0 ? cv::INTER_AREA : cv::INTER_CUBIC);

    std::vector<float> values(resized.begin<unsigned char>(), resized.end<unsigned char>());
    const float low = Percentile(values, 0.02);
    const float high = Percentile(values, 0.98);
    const float span = std::max(8.0f, high - low);

    cv::Mat input(kInputHeight, width, CV_32F);

    for (int y = 0; y < kInputHeight; ++y)
    {
        const unsigned char* source = resized.ptr<unsigned char>(y);
        float* target = input.ptr<float>(y);

        for (int x = 0; x < width; ++x)
            target[x] = std::clamp((source[x] - low) / span, 0.0f, 1.0f);
    }

    return input;
}

LineReader::Tensor LineReader::Convolve(const Tensor& input, const Layer& layer, int padHeight, int padWidth, bool relu)
{
    Tensor output;
    output.channels = layer.outputs;
    output.height = input.height + 2 * padHeight - layer.kernelHeight + 1;
    output.width = input.width + 2 * padWidth - layer.kernelWidth + 1;
    output.data.assign(static_cast<std::size_t>(output.channels) * output.height * output.width, 0.0f);

    const int paddedHeight = input.height + 2 * padHeight;
    const int paddedWidth = input.width + 2 * padWidth;

    std::vector<float> padded(static_cast<std::size_t>(input.channels) * paddedHeight * paddedWidth, 0.0f);

    for (int c = 0; c < input.channels; ++c)
    {
        for (int y = 0; y < input.height; ++y)
        {
            std::memcpy(
                padded.data() + (static_cast<std::size_t>(c) * paddedHeight + y + padHeight) * paddedWidth + padWidth,
                input.Plane(c) + static_cast<std::size_t>(y) * input.width,
                sizeof(float) * static_cast<std::size_t>(input.width)
            );
        }
    }

    const int kernelArea = layer.kernelHeight * layer.kernelWidth;

    for (int o = 0; o < output.channels; ++o)
    {
        float* out = output.Plane(o);

        for (int c = 0; c < input.channels; ++c)
        {
            const float* weights = layer.weights.data() + (static_cast<std::size_t>(o) * input.channels + c) * kernelArea;
            const float* plane = padded.data() + static_cast<std::size_t>(c) * paddedHeight * paddedWidth;

            for (int ky = 0; ky < layer.kernelHeight; ++ky)
            {
                for (int kx = 0; kx < layer.kernelWidth; ++kx)
                {
                    const float weight = weights[ky * layer.kernelWidth + kx];

                    for (int y = 0; y < output.height; ++y)
                    {
                        const float* source = plane + static_cast<std::size_t>(y + ky) * paddedWidth + kx;
                        float* target = out + static_cast<std::size_t>(y) * output.width;

                        for (int x = 0; x < output.width; ++x)
                            target[x] += weight * source[x];
                    }
                }
            }
        }

        const float bias = layer.bias[static_cast<std::size_t>(o)];
        const std::size_t size = static_cast<std::size_t>(output.height) * output.width;

        for (std::size_t i = 0; i < size; ++i)
        {
            const float value = out[i] + bias;
            out[i] = relu ? std::max(0.0f, value) : value;
        }
    }

    return output;
}

LineReader::Tensor LineReader::Pool(const Tensor& input, int poolHeight, int poolWidth)
{
    Tensor output;
    output.channels = input.channels;
    output.height = input.height / poolHeight;
    output.width = input.width / poolWidth;
    output.data.resize(static_cast<std::size_t>(output.channels) * output.height * output.width);

    for (int c = 0; c < input.channels; ++c)
    {
        const float* in = input.Plane(c);
        float* out = output.Plane(c);

        for (int y = 0; y < output.height; ++y)
        {
            const float* top = in + static_cast<std::size_t>(y) * poolHeight * input.width;
            float* target = out + static_cast<std::size_t>(y) * output.width;

            for (int x = 0; x < output.width; ++x)
            {
                const float* cell = top + static_cast<std::size_t>(x) * poolWidth;
                float best = cell[0];

                for (int dy = 0; dy < poolHeight; ++dy)
                {
                    const float* line = cell + static_cast<std::size_t>(dy) * input.width;

                    for (int dx = 0; dx < poolWidth; ++dx)
                        best = std::max(best, line[dx]);
                }

                target[x] = best;
            }
        }
    }

    return output;
}

LineReader::Result LineReader::Read(const cv::Mat& gray) const
{
    Result result;

    if (!Loaded())
        return result;

    const cv::Mat prepared = Prepare(gray);

    if (prepared.empty())
        return result;

    Tensor input;
    input.channels = 1;
    input.height = kInputHeight;
    input.width = (prepared.cols + kStride - 1) / kStride * kStride;
    input.data.assign(static_cast<std::size_t>(input.height) * input.width, 1.0f);

    for (int y = 0; y < kInputHeight; ++y)
    {
        std::memcpy(
            input.data.data() + static_cast<std::size_t>(y) * input.width,
            prepared.ptr<float>(y),
            sizeof(float) * static_cast<std::size_t>(prepared.cols)
        );
    }

    Tensor t = Convolve(input, layers_[0], 1, 1, true);
    t = Pool(t, 2, 2);
    t = Convolve(t, layers_[1], 1, 1, true);
    t = Pool(t, 2, 2);
    t = Convolve(t, layers_[2], 1, 1, true);
    t = Pool(t, 2, 1);
    t = Convolve(t, layers_[3], 1, 1, true);
    t = Convolve(t, layers_[4], 0, 0, true);
    t = Convolve(t, layers_[5], 0, 1, true);
    t = Convolve(t, layers_[6], 0, 1, true);
    const Tensor logits = Convolve(t, layers_[7], 0, 0, false);

    const int steps = std::min(logits.width, prepared.cols / kStride);
    int previous = 0;
    double confidence = 0.0;
    int emitted = 0;

    for (int step = 0; step < steps; ++step)
    {
        int best = 0;
        float bestValue = logits.Plane(0)[step];

        for (int k = 1; k < logits.channels; ++k)
        {
            const float value = logits.Plane(k)[step];

            if (value > bestValue)
            {
                bestValue = value;
                best = k;
            }
        }

        double sum = 0.0;

        for (int k = 0; k < logits.channels; ++k)
            sum += std::exp(static_cast<double>(logits.Plane(k)[step] - bestValue));

        if (best != 0 && best != previous)
        {
            result.text += charset_[static_cast<std::size_t>(best - 1)];
            confidence += 1.0 / sum;
            ++emitted;
        }

        previous = best;
    }

    result.confidence = emitted > 0 ? static_cast<float>(100.0 * confidence / emitted) : 0.0f;
    return result;
}
