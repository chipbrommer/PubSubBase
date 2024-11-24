
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "PubSubBase.hpp"

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

    // Main loop to send messages
    while (true)
    {
        static int msgCounter = 0;
        if (pub.SendMessage("test", "Count = " + std::to_string(msgCounter)))
        {
            msgCounter++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
