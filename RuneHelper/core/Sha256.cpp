#include "core/Sha256.h"

#include <cstdio>

namespace
{
constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t Rotate(std::uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}
}

void Sha256::Update(std::string_view data)
{
    length_ += data.size();

    for (const char c : data)
        Absorb(static_cast<unsigned char>(c));
}

std::string Sha256::FinishHex()
{
    const std::uint64_t bits = length_ * 8;

    Absorb(0x80);

    while (blockSize_ != 56)
        Absorb(0);

    for (int shift = 56; shift >= 0; shift -= 8)
        Absorb(static_cast<unsigned char>(bits >> shift));

    std::string hex;
    char word[9];

    for (const std::uint32_t value : state_)
    {
        std::snprintf(word, sizeof(word), "%08x", static_cast<unsigned>(value));
        hex += word;
    }

    return hex;
}

void Sha256::Absorb(unsigned char byte)
{
    block_[blockSize_++] = byte;

    if (blockSize_ == block_.size())
    {
        Transform();
        blockSize_ = 0;
    }
}

void Sha256::Transform()
{
    std::array<std::uint32_t, 64> schedule{};

    for (std::size_t i = 0; i < 16; ++i)
    {
        schedule[i] = (std::uint32_t{ block_[i * 4] } << 24) | (std::uint32_t{ block_[i * 4 + 1] } << 16) |
                      (std::uint32_t{ block_[i * 4 + 2] } << 8) | std::uint32_t{ block_[i * 4 + 3] };
    }

    for (std::size_t i = 16; i < schedule.size(); ++i)
    {
        const std::uint32_t s0 = Rotate(schedule[i - 15], 7) ^ Rotate(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
        const std::uint32_t s1 = Rotate(schedule[i - 2], 17) ^ Rotate(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }

    std::array<std::uint32_t, 8> v = state_;

    for (std::size_t i = 0; i < schedule.size(); ++i)
    {
        const std::uint32_t s1 = Rotate(v[4], 6) ^ Rotate(v[4], 11) ^ Rotate(v[4], 25);
        const std::uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + s1 + choice + kRoundConstants[i] + schedule[i];
        const std::uint32_t s0 = Rotate(v[0], 2) ^ Rotate(v[0], 13) ^ Rotate(v[0], 22);
        const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);

        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + s0 + majority;
    }

    for (std::size_t i = 0; i < state_.size(); ++i)
        state_[i] += v[i];
}

std::string Sha256Hex(std::string_view data)
{
    Sha256 hash;
    hash.Update(data);
    return hash.FinishHex();
}
