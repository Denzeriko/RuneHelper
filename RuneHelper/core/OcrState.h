#pragma once

enum class OcrState
{
    Stopped,
    Initializing,
    Ready,
    Failed
};

struct OcrStatus
{
    OcrState state = OcrState::Initializing;
    bool captureFailing = false;
};
