#pragma once

#include <cstdint>
#include <array>
#include <stdexcept>

#pragma pack(push,1)
struct status_msg
{
    uint8_t sync1 = 1;
    uint8_t sync2 = 2;
    uint32_t status1 = 0;
    uint32_t status2 = 0;
    uint8_t eob = 3;

    static constexpr std::size_t msgSize =
        sizeof(sync1) + sizeof(sync2) + sizeof(status1) + sizeof(status2) + sizeof(eob);

    std::array<uint8_t, msgSize> toArray() const
    {
        std::array<uint8_t, msgSize> arr{};
        std::memcpy(arr.data(), this, msgSize);
        return arr;
    }

    static status_msg fromArray(const uint8_t* in, std::size_t size)
    {
        // Check if the size matches the expected message size
        if (size != msgSize)
        {
            throw std::invalid_argument("Input data size does not match expected status_msg size");
        }

        status_msg out;
        std::memcpy(&out, in, msgSize);
        return out;
    }
};
#pragma pack(pop)