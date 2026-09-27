#pragma once

#include <cstddef>
#include <string>
#include <string_view>

inline constexpr char32_t kReplacementCharacter = 0xFFFD;

char32_t DecodeUtf8(std::string_view text, std::size_t& i);
std::u32string DecodeUtf8(std::string_view text);
void AppendUtf8(std::string& out, char32_t codePoint);
std::size_t CountCodePoints(std::string_view text);

std::string_view Trim(std::string_view text);
std::string ToLowerAscii(std::string_view text);
