#pragma once

enum class GameFocus
{
    Active,
    Inactive,
    Unknown
};

bool GameFocusSupported();
GameFocus QueryGameFocus();
