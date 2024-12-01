
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "PubSubBase.hpp"
#include "count_msg.h"
#include "status_msg.h"
#include "info_msg.h"

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
        if (pub.SendMessage("count", messageArray.data(), messageArray.size()))
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
        if (pub.SendMessage("status", messageArray.data(), messageArray.size()))
        {
            // toggle status 1 and have status 2 inverse status 1. 
            message.status1 = message.status1 == 1 ? 0 : 1;
            message.status2 = message.status1 == 1 ? 0 : 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void client3(std::shared_ptr<spdlog::logger> logger)
{
    logger->info("Client 3 started\n");

    // Create publisher instance
    PubSubBase pub(
        PubSubBase::Mode::PUBLISH_AND_SUBSCRIBE,
        { "tcp://127.0.0.1:5557" },
        { "tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556" },
        [logger](const std::string& message) { logger->error(message); },
        [logger](const std::string& message) { logger->info(message); }
    );

    pub.AddSubscribedTopic("count");
    pub.AddSubscribedTopic("status");

    std::string topic;
    std::array<uint8_t, 100> data{ 0 };
    count_msg cm{};
    status_msg sm{};
    info_msg im{};
    bool newData = false;

    // Main loop to send messages
    while (!kill)
    {
        int read = pub.ReceiveMessage(topic, data.data(), data.size());

        if (read <= 0)
        {
            continue;
        }

        if (topic == "count")
        {
            if (read != count_msg::msgSize)
            {
                break;
            }

            cm = count_msg::fromArray(data.data(), read);
            im.count = cm.count;
            newData = true;
        }
        else if (topic == "status")
        {
            if (read != status_msg::msgSize)
            {
                break;
            }

            sm = status_msg::fromArray(data.data(), read);
            im.status1 = sm.status1;
            im.status2 = sm.status2;
            newData = true;
        }

        if (newData)
        {
            auto messageArray = im.toArray();
            if (!pub.SendMessage("info", messageArray.data(), messageArray.size()))
            {
                logger->error("Client 3 failed to send!\n");
            }
            newData = false;
        }
    
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void server1(std::shared_ptr<spdlog::logger> logger)
{
    logger->info("Server 1 started\n");

    // Create publisher instance
    PubSubBase pub(
        PubSubBase::Mode::SUBSCRIBE,
        {""},
        {"tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556", "tcp://127.0.0.1:5557"},
        [logger](const std::string& message) { logger->error(message); },
        [logger](const std::string& message) { logger->info(message); }
    );

    pub.AddSubscribedTopic("count");
    pub.AddSubscribedTopic("status");
    pub.AddSubscribedTopic("info");

    std::string topic;
    std::array<uint8_t, 100> data{ 0 };

    // Main loop to read messages
    while (!kill)
    {
        int read = pub.ReceiveMessage(topic, data.data(), data.size(), true);

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
        else if (topic == "info")
        {
            if (read != info_msg::msgSize)
            {
                logger->info("Invalid 'info' read\n");
                break;
            }

            info_msg im = info_msg::fromArray(data.data(), read);
            std::cout << "Info: " << im.count << " = " << im.status1 << " " << im.status2 << "\n";

            if (im.count >= 100)
            {
                kill = true;
            }
        }
        else
        {
            logger->info("Unknown topic: " + topic.empty() ? "EMPTY!" : topic + "\n");
        }
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

    std::thread c3([logger]()
        {
            client3(logger);
        });
    
    std::thread s1([logger]()
        {
            server1(logger);
        });

    while (!kill)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    kill = true;

    c1.join();
    c2.join();
    c3.join();
    s1.join();

    return 0;
}