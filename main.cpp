#include <array>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "PubSubBase.hpp"

#pragma pack(push,1)
struct msg
{
    uint8_t sync1 = 1;
    uint8_t sync2 = 2;
    uint32_t counter = 0;
    uint8_t eob = 3;

    static constexpr std::size_t msgSize =
        sizeof(sync1) + sizeof(sync2) + sizeof(counter) + sizeof(eob);

    std::array<uint8_t, msgSize> toArray() const
    {
        std::array<uint8_t, msgSize> arr{};
        std::memcpy(arr.data(), this, msgSize);
        return arr;
    }
};
#pragma pack(pop)

int main()
{
    // Initialize spdlog
    auto logger = spdlog::stdout_color_mt("console"); // Console logger
    logger->set_level(spdlog::level::info);          // Set logging level

    // Log ZMQ version using spdlog
    logger->info("Starting Publisher...");
    logger->info("ZMQ_VERSION_MAJOR: {}", ZMQ_VERSION_MAJOR);
    logger->info("ZMQ_VERSION_MINOR: {}", ZMQ_VERSION_MINOR);

    // Create publisher instance
    PubSubBase pub(
        PubSubBase::Mode::PUBLISH, 
        "tcp://127.0.0.1:5555",
        "",
        [logger](const std::string& message) { logger->error(message); },
        [logger](const std::string& message) { logger->info(message); }
    );

    msg message{};

    // Main loop to send messages
    while (true)
    {
        auto messageArray = message.toArray();
        if (pub.SendMessage("test", messageArray, messageArray.size()))
        {
            message.counter++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
