#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

class Sha256
{
public:
    void Update(std::string_view data);
    std::string FinishHex();

private:
    void Absorb(unsigned char byte);
    void Transform();

    std::array<std::uint32_t, 8> state_{ 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::array<unsigned char, 64> block_{};
    std::size_t blockSize_ = 0;
    std::uint64_t length_ = 0;
};

std::string Sha256Hex(std::string_view data);
