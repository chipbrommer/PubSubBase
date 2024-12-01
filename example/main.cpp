
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "PubSubBase.hpp"
#include "count_msg.h"
#include "status_msg.h"

std::atomic_bool kill{ false };

void client1(std::shared_ptr<spdlog::logger> logger)
{
    logger->info("Client 1 started\n");

    // Create publisher instance
    PubSubBase pub(
        PubSubBase::Mode::PUBLISH,
        { "tcp://127.0.0.1:5555" },
        {""},
        [logger](const std::string& message) { logger->error(message); },
        [logger](const std::string& message) { logger->info(message); }
    );

    count_msg message{};

    // Main loop to send messages
    while (!kill)
    {
        auto messageArray = message.toArray();
        if (pub.SendMessage("count", messageArray, messageArray.size()))
        {
            message.count++;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void client2(std::shared_ptr<spdlog::logger> logger)
{
    logger->info("Client 2 started\n");

    // Create publisher instance
    PubSubBase pub(
        PubSubBase::Mode::PUBLISH,
        { "tcp://127.0.0.1:5556" },
        {""},
        [logger](const std::string& message) { logger->error(message); },
        [logger](const std::string& message) { logger->info(message); }
    );

    status_msg message{};

    // Main loop to send messages
    while (!kill)
    {
        auto messageArray = message.toArray();
        if (pub.SendMessage("status", messageArray, messageArray.size()))
        {
            // toggle status 1 and have status 2 inverse status 1. 
            message.status1 = message.status1 == 1 ? 0 : 1;
            message.status2 = message.status1 == 1 ? 0 : 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void server1(std::shared_ptr<spdlog::logger> logger)
{
    logger->info("Server 1 started\n");

    // Create publisher instance
    PubSubBase pub(
        PubSubBase::Mode::SUBSCRIBE,
        {"tcp://127.0.0.1:5557"},
        {"tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556" },
        [logger](const std::string& message) { logger->error(message); },
        [logger](const std::string& message) { logger->info(message); }
    );

    pub.AddSubscribedTopic("count");
    pub.AddSubscribedTopic("status");

    std::string topic;
    std::array<uint8_t, 100> data{ 0 };

    // Main loop to read messages
    while (!kill)
    {
        int read = pub.ReceiveMessage(topic, data);

        if (topic == "count")
        {
            if (read != count_msg::msgSize)
            {
                logger->info("Invalid 'count' read\n");
                break;
            }

            count_msg cm = count_msg::fromArray(data.data(), read);
            std::cout << "Count: " << cm.count << "\n";
        }
    
        else if (topic == "status")
        {
            if (read != status_msg::msgSize)
            {
                logger->info("Invalid 'status' read\n");
                break;
            }

            status_msg sm = status_msg::fromArray(data.data(), read);
            std::cout << "Status: " << sm.status1 << " " << sm.status2 << "\n";
        }
        else
        {
            logger->info("Unknown topic: " + topic.empty() ? "EMPTY!" : topic + "\n");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main()
{
    // Initialize spdlog
    auto logger = spdlog::stdout_color_mt("console"); // Console logger
    logger->set_level(spdlog::level::info);          // Set logging level

    // Log ZMQ version using spdlog
    logger->info("Starting Publisher...");
    logger->info("ZMQ_VERSION_MAJOR: {}", ZMQ_VERSION_MAJOR);
    logger->info("ZMQ_VERSION_MINOR: {}", ZMQ_VERSION_MINOR);

    // Pass logger to client threads
    std::thread c1([logger]()
        {
            client1(logger);
        });

    std::thread c2([logger]()
        {
            client2(logger);
        });
    
    std::thread s1([logger]()
        {
            server1(logger);
        });

    int k = 0;
    while (k < 100)
    {
        k++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    kill = true;

    c1.join();
    c2.join();
    s1.join();

    return 0;
}