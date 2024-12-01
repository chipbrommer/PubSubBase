#pragma once

#include <cstdint>
#include <array>
#include <stdexcept>

#pragma pack(push,1)
struct count_msg
{
    uint8_t sync1 = 1;
    uint8_t sync2 = 2;
    uint32_t count = 0;
    uint8_t eob = 3;

    static constexpr std::size_t msgSize =
        sizeof(sync1) + sizeof(sync2) + sizeof(count) + sizeof(eob);

    std::array<uint8_t, msgSize> toArray() const
    {
        std::array<uint8_t, msgSize> arr{};
        std::memcpy(arr.data(), this, msgSize);
        return arr;
    }

    static count_msg fromArray(const uint8_t* in, std::size_t size)
    {
        // Check if the size matches the expected message size
        if (size != msgSize)
        {
            throw std::invalid_argument("Input data size does not match expected count_msg size");
        }

        count_msg out;
        std::memcpy(&out, in, msgSize); 
        return out;
    }
};
#pragma pack(pop)