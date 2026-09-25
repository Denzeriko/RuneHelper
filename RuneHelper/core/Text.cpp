#include "core/Text.h"

#include <cctype>

char32_t DecodeUtf8(std::string_view text, std::size_t& i)
{
    const unsigned char lead = static_cast<unsigned char>(text[i++]);

    if (lead < 0x80)
        return lead;

    int extra = 0;
    char32_t codePoint = 0;

    if ((lead & 0xE0) == 0xC0)
    {
        extra = 1;
        codePoint = lead & 0x1F;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        extra = 2;
        codePoint = lead & 0x0F;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        extra = 3;
        codePoint = lead & 0x07;
    }
    else
    {
        return kReplacementCharacter;
    }

    for (int k = 0; k < extra; ++k)
    {
        if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            return kReplacementCharacter;

        codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[i++]) & 0x3F);
    }

    return codePoint;
}

std::u32string DecodeUtf8(std::string_view text)
{
    std::u32string decoded;
    decoded.reserve(text.size());

    for (std::size_t i = 0; i < text.size();)
        decoded.push_back(DecodeUtf8(text, i));

    return decoded;
}

void AppendUtf8(std::string& out, char32_t codePoint)
{
    if (codePoint < 0x80)
    {
        out.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

std::size_t CountCodePoints(std::string_view text)
{
    std::size_t count = 0;

    for (unsigned char ch : text)
    {
        if ((ch & 0xC0) != 0x80)
            ++count;
    }

    return count;
}

std::string_view Trim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;

    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;

    return text.substr(begin, end - begin);
}

std::string ToLowerAscii(std::string_view text)
{
    std::string lower(text);

    for (char& ch : lower)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }

    return lower;
}
