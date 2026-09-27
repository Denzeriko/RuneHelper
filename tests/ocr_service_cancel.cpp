#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stop_token>

#include <opencv2/core.hpp>

#include "core/ConfigManager.h"
#include "core/Feature.h"
#include "core/OcrService.h"
#include "platform/GameFocus.h"
#include "platform/linux/ScreenCapture.h"
#include "price/PriceService.h"

namespace
{
class BlockingCapture final : public IScreenCapture
{
public:
    cv::Mat CaptureRegion(const cv::Rect&, const std::stop_token& stop) override
    {
        std::unique_lock lock(mutex_);
        entered_ = true;
        condition_.notify_all();
        condition_.wait_for(lock, std::chrono::seconds(2), [this] { return cancelled_; });
        returnedAfterCancel_ = cancelled_ && stop.stop_requested();
        return {};
    }

    void Cancel() override
    {
        {
            std::lock_guard lock(mutex_);
            cancelled_ = true;
        }

        condition_.notify_all();
    }

    void Shutdown() override {}

    bool WaitUntilEntered()
    {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(10), [this] { return entered_; });
    }

    bool ReturnedAfterCancel()
    {
        std::lock_guard lock(mutex_);
        return returnedAfterCancel_;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool entered_ = false;
    bool cancelled_ = false;
    bool returnedAfterCancel_ = false;
};
}

GameFocus QueryGameFocus()
{
    return GameFocus::Unknown;
}

bool GameFocusSupported()
{
    return false;
}

cv::Mat CaptureRegion(const cv::Rect&, const std::stop_token&)
{
    return {};
}

void CancelCapture() {}

int main()
{
    ConfigManager config;
    config.Update(
        [](AppConfig& value)
        {
            value.regionW = 20;
            value.regionH = 20;
            value.pauseWhenGameInactive = false;
        }
    );

    FeatureRegistry features;
    PriceService prices;
    auto capture = std::make_unique<BlockingCapture>();
    BlockingCapture* observed = capture.get();
    OcrService service(config, features, prices, std::move(capture));

    service.Start();
    const bool entered = observed->WaitUntilEntered();
    service.Stop();

    if (!entered || !observed->ReturnedAfterCancel())
    {
        std::fprintf(stderr, "OcrService did not cancel its active screen capture\n");
        return 1;
    }

    return 0;
}
