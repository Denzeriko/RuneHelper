#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "core/ConfigManager.h"
#include "core/Feature.h"
#include "core/OcrService.h"
#include "platform/GameFocus.h"
#include "platform/PlatformPaths.h"
#include "platform/linux/ScreenCapture.h"
#include "price/PriceService.h"

namespace
{
using namespace std::chrono_literals;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Predicate>
void Wait(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate())
    {
        Require(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(1ms);
    }
}

enum class Reply
{
    Frame,
    Empty,
    Error
};

class ControlledCapture final : public IScreenCapture
{
public:
    cv::Mat CaptureRegion(const cv::Rect& region, const std::stop_token& stop) override
    {
        std::unique_lock lock(mutex_);
        ++entered_;
        condition_.notify_all();
        if (!condition_.wait_for(lock, 10s, [this] { return cancelled_ || !replies_.empty(); }))
            throw std::runtime_error("capture reply timed out");

        if (cancelled_)
        {
            returnedAfterCancel_ = stop.stop_requested();
            return lateFrame_ ? cv::Mat(region.size(), CV_8UC1, cv::Scalar(0)) : cv::Mat{};
        }

        auto [reply, image] = std::move(replies_.front());
        replies_.pop_front();
        if (reply == Reply::Error)
            throw std::runtime_error("capture failed");
        if (reply == Reply::Empty)
            return {};
        return image.empty() ? cv::Mat(region.size(), CV_8UC1, cv::Scalar(0)) : image;
    }

    void Cancel() override
    {
        std::lock_guard lock(mutex_);
        cancelled_ = true;
        condition_.notify_all();
    }

    void Shutdown() override
    {
        std::lock_guard lock(mutex_);
        cancelled_ = false;
        replies_.clear();
    }

    void Respond(Reply reply, cv::Mat image = {})
    {
        std::lock_guard lock(mutex_);
        replies_.emplace_back(reply, std::move(image));
        condition_.notify_all();
    }

    void WaitForCapture(unsigned count)
    {
        std::unique_lock lock(mutex_);
        Require(condition_.wait_for(lock, 10s, [&] { return entered_ >= count; }), "capture did not start");
    }

    bool HasCaptureWithin(std::chrono::milliseconds duration)
    {
        std::unique_lock lock(mutex_);
        const unsigned count = entered_;
        return condition_.wait_for(lock, duration, [&] { return entered_ != count; });
    }

    unsigned Captures()
    {
        std::lock_guard lock(mutex_);
        return entered_;
    }

    void ReturnFrameAfterCancel()
    {
        std::lock_guard lock(mutex_);
        lateFrame_ = true;
    }

    bool ReturnedAfterCancel()
    {
        std::lock_guard lock(mutex_);
        return returnedAfterCancel_;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::pair<Reply, cv::Mat>> replies_;
    unsigned entered_ = 0;
    bool cancelled_ = false;
    bool returnedAfterCancel_ = false;
    bool lateFrame_ = false;
};

class FrameMarker final : public Feature
{
public:
    std::string Name() const override { return "Frame marker"; }

    void OnFrame(FrameContext& frame) override
    {
        const std::string marker = std::to_string(++frames);
        if (requireLoot && frame.rows.empty())
            return;
        frame.overlay.texts.push_back({ marker });
        frame.debug.lines.push_back({ marker, {}, {} });
    }

    std::atomic<unsigned> frames = 0;
    bool requireLoot = false;
};

struct Fixture
{
    ConfigManager config;
    FeatureRegistry features;
    PriceService prices;
    ControlledCapture* capture = nullptr;
    FrameMarker* marker = nullptr;
    std::unique_ptr<OcrService> service;

    explicit Fixture(bool enabled = true)
    {
        config.Update(
            [&](AppConfig& value)
            {
                value.regionW = 80;
                value.regionH = 80;
                value.ocrEnabled = enabled;
                value.pauseWhenGameInactive = false;
            }
        );
        auto feature = std::make_unique<FrameMarker>();
        marker = feature.get();
        features.Add(std::move(feature));
        auto screen = std::make_unique<ControlledCapture>();
        capture = screen.get();
        service = std::make_unique<OcrService>(config, features, prices, std::move(screen));
    }

    void Start()
    {
        service->Start();
        Wait([&] { return service->Status().state == OcrState::Ready; }, "OCR did not become ready");
    }

    void Frame(unsigned captureNumber, unsigned frameNumber = 1)
    {
        capture->WaitForCapture(captureNumber);
        capture->Respond(Reply::Frame);
        DebugData debug;
        Wait([&] { return service->ConsumeDebugData(debug); }, "frame result was not published");
        Require(debug.lines.size() == 1 && debug.lines.front().ocrText == std::to_string(frameNumber), "wrong debug result");
        OverlayFrame overlay;
        Require(service->ConsumeOverlayFrame(overlay), "overlay was not published");
        Require(overlay.texts.size() == 1 && overlay.texts.front().text == std::to_string(frameNumber), "wrong overlay result");
    }

    void RequireCleared()
    {
        const OcrStatus status = service->Status();
        Require(status.state == OcrState::Stopped && !status.captureFailing && !status.waitingForGame, "status was not reset");
        DebugData debug;
        OverlayFrame overlay;
        Require(!service->ConsumeDebugData(debug), "stale debug result after stop");
        Require(!service->ConsumeOverlayFrame(overlay), "stale overlay after stop");
    }
};

void TestRestart()
{
    Fixture fixture;
    fixture.Start();
    fixture.capture->WaitForCapture(1);
    fixture.capture->Respond(Reply::Frame);
    fixture.capture->WaitForCapture(2);
    Require(fixture.marker->frames == 1, "frame was not processed");
    fixture.service->RequestSingleSnapshot();
    fixture.service->RequestDebugDump();
    fixture.service->Stop();
    fixture.RequireCleared();

    fixture.config.Update([](AppConfig& value) { value.ocrEnabled = false; });
    fixture.service->RequestSingleSnapshot();
    fixture.service->RequestDebugDump();
    fixture.Start();
    Require(!fixture.capture->HasCaptureWithin(300ms) && fixture.capture->Captures() == 2, "old commands survived restart");
    DebugData debug;
    OverlayFrame overlay;
    Require(!fixture.service->ConsumeDebugData(debug), "old debug data survived restart");
    Require(!fixture.service->ConsumeOverlayFrame(overlay), "old overlay survived restart");
    fixture.service->RequestSingleSnapshot();
    fixture.Frame(3, 2);
    Require(fixture.service->DebugDumpsWritten() == 0, "old dump command survived restart");
}

void TestRecovery()
{
    Fixture fixture;
    fixture.Start();
    for (unsigned attempt = 1; attempt <= 3; ++attempt)
    {
        fixture.capture->WaitForCapture(attempt);
        fixture.capture->Respond(Reply::Empty);
    }
    fixture.capture->WaitForCapture(4);
    Require(fixture.service->Status().captureFailing, "missing capture failure status");
    fixture.Frame(4);
    Require(!fixture.service->Status().captureFailing, "capture failure status did not clear");
    fixture.capture->WaitForCapture(5);
    fixture.capture->Respond(Reply::Error);
    fixture.Frame(6, 2);
    Require(fixture.marker->frames == 2, "failed captures reached features");
}

void TestDisabledCommands()
{
    Fixture fixture(false);
    fixture.Start();
    Require(!fixture.capture->HasCaptureWithin(200ms) && fixture.capture->Captures() == 0, "disabled OCR captured a frame");
    fixture.service->RequestSingleSnapshot();
    fixture.Frame(1);
    Require(fixture.service->DebugDumpsWritten() == 0, "snapshot created a debug dump");
    fixture.capture->WaitForCapture(2);
    std::this_thread::sleep_for(2100ms);
    fixture.capture->Respond(Reply::Empty);
    Require(!fixture.capture->HasCaptureWithin(300ms) && fixture.capture->Captures() == 2, "snapshot window did not expire");
    fixture.service->RequestDebugDump();
    fixture.Frame(3, 2);
    Require(fixture.service->DebugDumpsWritten() == 1, "debug dump was not processed");
    Require(std::filesystem::exists(GetUserDataDir() / "ocr_debug/latest/source.png"), "debug image was not saved");
}

void TestMenuClose(const char* imagePath)
{
    const cv::Mat panel = cv::imread(imagePath, cv::IMREAD_GRAYSCALE);
    Require(!panel.empty(), "test panel did not load");
    Fixture fixture;
    fixture.marker->requireLoot = true;
    fixture.config.Update(
        [&](AppConfig& value)
        {
            value.regionW = panel.cols;
            value.regionH = panel.rows;
        }
    );
    fixture.Start();
    fixture.capture->WaitForCapture(1);
    fixture.capture->Respond(Reply::Frame, panel);
    fixture.capture->WaitForCapture(2);
    OverlayFrame overlay;
    Require(fixture.service->ConsumeOverlayFrame(overlay) && !overlay.Empty(), "loot overlay was not shown");

    const auto closedAt = std::chrono::steady_clock::now();
    fixture.capture->Respond(Reply::Frame);
    fixture.capture->WaitForCapture(3);
    if (fixture.service->ConsumeOverlayFrame(overlay))
        Require(!overlay.Empty(), "one transient frame hid the overlay");

    fixture.capture->Respond(Reply::Frame, cv::Mat(panel.size(), CV_8UC1, cv::Scalar(30)));
    Wait([&] { return fixture.service->ConsumeOverlayFrame(overlay) && overlay.Empty(); }, "closed menu kept a stale overlay");
    std::printf(
        "menu clear after first closed frame: %.1f ms\n",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - closedAt).count()
    );

    fixture.capture->WaitForCapture(4);
    fixture.capture->Respond(Reply::Frame);
    fixture.capture->WaitForCapture(5);
    if (fixture.service->ConsumeOverlayFrame(overlay))
        Require(overlay.Empty(), "stale overlay reappeared over the game");

    bool reopened = false;
    for (unsigned capture = 5; capture < 17 && !reopened; ++capture)
    {
        fixture.capture->Respond(Reply::Frame, panel);
        fixture.capture->WaitForCapture(capture + 1);
        reopened = fixture.service->ConsumeOverlayFrame(overlay) && !overlay.Empty();
    }
    Require(reopened, "overlay did not return when the menu reopened");
}

void TestLateFrame()
{
    Fixture fixture;
    fixture.capture->ReturnFrameAfterCancel();
    fixture.Start();
    fixture.capture->WaitForCapture(1);
    fixture.service->Stop();
    Require(fixture.capture->ReturnedAfterCancel(), "capture was not cancelled");
    Require(fixture.marker->frames == 0, "cancelled capture reached frame processing");
    fixture.RequireCleared();
}
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

void ShutdownCapture() {}

int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    std::string temporary = (std::filesystem::temp_directory_path() / "runehelper-ocr-service-XXXXXX").string();
    if (!mkdtemp(temporary.data()))
        return 1;
    if (setenv("XDG_CONFIG_HOME", temporary.c_str(), 1) != 0)
        return 1;

    int failures = 0;
    const auto run = [&](const char* name, auto test)
    {
        try
        {
            test();
            std::printf("PASS %s\n", name);
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::fprintf(stderr, "FAIL %s: %s\n", name, error.what());
        }
    };
    run("cancel active capture and discard late frame", TestLateFrame);
    run("restart clears commands and results", TestRestart);
    run("capture recovery", TestRecovery);
    run("close and reopen loot menu", [&] { TestMenuClose(argv[1]); });
    run("snapshot and debug dump with disabled OCR", TestDisabledCommands);
    std::filesystem::remove_all(temporary);
    return failures == 0 ? 0 : 1;
}
