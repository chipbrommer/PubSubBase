#pragma once
/////////////////////////////////////////////////////////////////////////////////
// @file            PubSubBase.hpp
// @brief           A class for communicating in Pub / Sub scenarios with ZMQ
// @author          Chip Brommer
/////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////
// @includes
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <zmq.hpp>
/////////////////////////////////////////////////////////////////////////////////

class PubSubBase 
{
public:
    /// @brief Available modes for the class functionality
    enum class Mode { PUBLISH, SUBSCRIBE, PUBLISH_AND_SUBSCRIBE};

    /// @brief Convenience definition for a std::function used for logging
    using LogHandler = std::function<void(const std::string&)>;

    /// @brief Default Constructor
    /// @param mode - Mode for the class instance
    /// @param pubEndpoint - vector of endpoints for the publisher
    /// @param subEndpoint - opt - vector of endpoints for the subscriber
    /// @param errorHandler - opt - std::function or lambda to be used as a callback for error messages
    /// @param infoHandler - opt - std::function or lambda to be used as a callback for info messages
    /// @param context - opt - pointer to a shared context or nullptr to have class create its own. 
    PubSubBase(Mode mode,
        const std::vector<std::string>& pubEndpoints = {},
        const std::vector<std::string>& subEndpoints = {},
        LogHandler errorHandler = [](const std::string&) {},
        LogHandler infoHandler = [](const std::string&) {},
        std::shared_ptr<zmq::context_t> context = nullptr)
        : m_Mode(mode),
        m_ErrorHandler(std::move(errorHandler)),
        m_InfoHandler(std::move(infoHandler))
    {
        InitializeContext(context);

        // Intentionally handle endpoints one by one so we can check for duplicates
        for (const auto& endpoint : pubEndpoints)
        {
            AddPublisherEndpoint(endpoint);
        }

        for (const auto& endpoint : subEndpoints)
        {
            AddSubscriberEndpoint(endpoint);
        }

        if (m_Mode == Mode::PUBLISH || m_Mode == Mode::PUBLISH_AND_SUBSCRIBE)
        {
            InitializePublisherSocket();
        }

        if (m_Mode == Mode::SUBSCRIBE || m_Mode == Mode::PUBLISH_AND_SUBSCRIBE)
        {
            InitializeSubscriberSocket();
        }
    }

    /// @brief Default deconstructor
    ~PubSubBase() 
    {
        CloseSockets();
    }

    PubSubBase(const PubSubBase&) = delete;
    PubSubBase& operator=(const PubSubBase&) = delete;
    PubSubBase(PubSubBase&&) = delete;
    PubSubBase& operator=(PubSubBase&&) = delete;

    /// @brief Updates the classes ZMQ context
    /// @param context - Pointer to a pre-existing ZMQ context or pass in nullptr to have class create its own context. 
    void SetContext(std::shared_ptr<zmq::context_t> context)
    {
        // If the existing context is owned, release it
        if (m_OwnContext && m_Context)
        {
            CloseSockets();
        }

        // Initialize context with the provided one or create a new one
        InitializeContext(context);

        // Re-initialize sockets with the new context
        ResetPublisherSocket();
        ResetSubscriberSocket();
    }

    /// @brief Adds a specific topic to the subscribed list
    /// @param topic - topic to be subscribed to
    void AddSubscribedTopic(std::string_view topic)
    {
        std::scoped_lock lock(m_SubMutex);
        if (m_SubSocket)
        {
            try
            {
                m_SubSocket->set(zmq::sockopt::subscribe, std::string(topic));
                LogInfo("Updated subscription to topics: " + std::string(topic));
            }
            catch (const zmq::error_t& e)
            {
                LogError("Failed to update subscription: " + std::string(e.what()));
            }
        }
        else
        {
            LogError("UpdateSubscribedTopics() called but no subscriber socket initialized");
        }
    }

    /// @brief Unsubscribe from a specific topic
    /// @param topic - topic to be unsubscribed from
    void UnsubscribeFromTopic(std::string_view topic)
    {
        std::scoped_lock lock(m_SubMutex);
        if (m_SubSocket)
        {
            try
            {
                m_SubSocket->set(zmq::sockopt::unsubscribe, std::string(topic));
                LogInfo("Unsubscribed from topic: " + std::string(topic));
            }
            catch (const zmq::error_t& e)
            {
                LogError("Failed to unsubscribe from topic: " + std::string(e.what()));
            }
        }
        else
        {
            LogError("UnsubscribeFromTopic() called but no subscriber socket initialized");
        }
    }

    /// @brief Will clear all subscribed topics.
    void ClearSubscribedTopics()
    {
        std::scoped_lock lock(m_SubMutex);
        if (m_SubSocket)
        {
            try
            {
                m_SubSocket->set(zmq::sockopt::unsubscribe, "");
                LogInfo("Cleared all topic subscriptions");
            }
            catch (const zmq::error_t& e)
            {
                LogError("Failed to clear subscriptions: " + std::string(e.what()));
            }
        }
        else
        {
            LogError("ClearSubscribedTopics() called but no subscriber socket initialized");
        }
    }

    /// @brief Sends message (used only in PUBLISH mode)
    /// @param topic - topic for the message being sent
    /// @param message - message to be transmitted as a std::array<uint8_t, N>
    /// @param message_size - the actual size of the data within the array to send
    /// @return true on successful send, else false.
    template <std::size_t N>
    [[nodiscard]] bool SendMessage(std::string_view topic, const std::array<uint8_t, N>& message, std::size_t message_size)
    {
        std::scoped_lock lock(m_PubMutex);
        if (!m_PubSocket)
        {
            LogError("SendMessage() called but no publisher socket initialized");
            return false;
        }

        if (topic.empty())
        {
            LogError("SendMessage() called with an empty topic");
            return false;
        }

        if (message_size > N)
        {
            LogError("SendMessage() called with message_size exceeding array capacity");
            return false;
        }

        try
        {
            zmq::message_t topic_msg(topic.data(), topic.size());
            zmq::message_t data_msg(message.data(), message_size);

            m_PubSocket->send(topic_msg, zmq::send_flags::sndmore);
            m_PubSocket->send(data_msg, zmq::send_flags::none);

            return true;
        }
        catch (const zmq::error_t& e)
        {
            LogError("Error sending message: " + std::string(e.what()));
        }

        return false;
    }

    /// @brief Send messages with a retry counter (used only in PUBLISH mode)
    /// @param topic - topic for the message being sent
    /// @param message - message to be transmitted as a std::array<uint8_t, N>
    /// @param message_size - the actual size of the data within the array to send
    /// @param retries - opt - number of retry attempts. Defaults to 3.
    /// @return true on successful send, else false.
    template <std::size_t N>
    [[nodiscard]] bool SendMessageWithRetry(std::string_view topic, const std::array<uint8_t, N>& message,
        std::size_t message_size, int retries = 3)
    {
        while (retries-- > 0)
        {
            if (SendMessage(topic, message, message_size))
            {
                return true;
            }
            LogError("Retrying message send...");
        }
        return false;
    }

    /// @brief Used to receive messages from the set endpoint (used only in SUBSCRIBE mode)
    /// @param topic - out - received topic as an std::string
    /// @param message - out - received message as a std::array<uint8_t, N>
    /// @param blocking - in / opt - true will block and wait for message, false by default for instant return
    /// @return number bytes read on successful read, else -1.
    template <std::size_t N>
    [[nodiscard]] int ReceiveMessage(std::string& topic, std::array<uint8_t, N>& message, bool blocking = false)
    {
        std::scoped_lock lock(m_SubMutex);
        if (!m_SubSocket)
        {
            LogError("ReceiveMessage() called but no subscriber socket initialized");
            return -1;
        }

        try
        {
            zmq::recv_flags flags = blocking ? zmq::recv_flags::none : zmq::recv_flags::dontwait;

            zmq::message_t topic_msg, data_msg;
            if (m_SubSocket->recv(topic_msg, flags) &&
                m_SubSocket->recv(data_msg, zmq::recv_flags::none))
            {
                topic.assign(static_cast<char*>(topic_msg.data()), topic_msg.size());

                // Ensure the data fits in the fixed-size array
                if (data_msg.size() > N)
                {
                    LogError("Received message size exceeds std::array capacity");
                    return -1;
                }

                // Copy data into the std::array
                std::memcpy(message.data(), data_msg.data(), data_msg.size());

                // Fill any unused portion of the array with zeroes
                if (data_msg.size() < N)
                {
                    std::fill(message.begin() + data_msg.size(), message.end(), 0);
                }

                return data_msg.size();
            }
        }
        catch (const zmq::error_t& e)
        {
            LogError("Error receiving message: " + std::string(e.what()));
        }

        return -1;
    }

    /// @brief Adds a publisher endpoint.
    /// @param endpoint - The endpoint to bind to.
    void AddPublisherEndpoint(const std::string& endpoint)
    {
        std::scoped_lock lock(m_PubMutex);

        if (std::find(m_PubEndpoints.begin(), m_PubEndpoints.end(), endpoint) != m_PubEndpoints.end())
        {
            m_ErrorHandler("Publisher endpoint already exists: " + endpoint);
            return;
        }

        m_PubEndpoints.push_back(endpoint);
        if (m_PubSocket)
        {
            try
            {
                m_PubSocket->bind(endpoint);
                LogInfo("Added publisher endpoint: " + endpoint);
            }
            catch (const zmq::error_t& e)
            {
                LogError("Failed to bind publisher endpoint: " + endpoint + ". Error: " + std::string(e.what()));
            }
        }
    }

    /// @brief Adds a subscriber endpoint.
    /// @param endpoint - The endpoint to connect to.
    void AddSubscriberEndpoint(const std::string& endpoint)
    {
        std::scoped_lock lock(m_SubMutex);

        if (std::find(m_SubEndpoints.begin(), m_SubEndpoints.end(), endpoint) != m_SubEndpoints.end())
        {
            m_ErrorHandler("Subscriber endpoint already exists: " + endpoint);
            return;
        }

        m_SubEndpoints.push_back(endpoint);
        if (m_SubSocket)
        {
            try
            {
                m_SubSocket->connect(endpoint);
                LogInfo("Added subscriber endpoint: " + endpoint);
            }
            catch (const zmq::error_t& e)
            {
                LogError("Failed to connect subscriber endpoint: " + endpoint + ". Error: " + std::string(e.what()));
            }
        }
    }

    /// @brief Removes a publisher endpoint.
    /// @param endpoint - The endpoint to unbind.
    void RemovePublisherEndpoint(const std::string& endpoint)
    {
        std::scoped_lock lock(m_PubMutex);

        // Check if the endpoint exists
        auto it = std::find(m_PubEndpoints.begin(), m_PubEndpoints.end(), endpoint);
        if (it != m_PubEndpoints.end())
        {
            try
            {
                if (m_PubSocket)
                {
                    m_PubSocket->unbind(endpoint);
                    LogInfo("Publisher endpoint unbound: " + endpoint);
                }
                // Remove from the list
                m_PubEndpoints.erase(it);
            }
            catch (const zmq::error_t& e)
            {
                LogError("Failed to unbind publisher endpoint: " + endpoint + ". Error: " + std::string(e.what()));
            }
        }
        else
        {
            LogInfo("Publisher endpoint not found: " + endpoint);
        }
    }

    /// @brief Removes a subscriber endpoint.
    /// @param endpoint - The endpoint to disconnect.
    void RemoveSubscriberEndpoint(const std::string& endpoint)
    {
        std::scoped_lock lock(m_SubMutex);

        // Check if the endpoint exists
        auto it = std::find(m_SubEndpoints.begin(), m_SubEndpoints.end(), endpoint);
        if (it != m_SubEndpoints.end())
        {
            try
            {
                if (m_SubSocket)
                {
                    m_SubSocket->disconnect(endpoint);
                    LogInfo("Subscriber endpoint disconnected: " + endpoint);
                }
                // Remove from the list
                m_SubEndpoints.erase(it);
            }
            catch (const zmq::error_t& e)
            {
                LogError("Failed to disconnect subscriber endpoint: " + endpoint + ". Error: " + std::string(e.what()));
            }
        }
        else
        {
            LogInfo("Subscriber endpoint not found: " + endpoint);
        }
    }

    /// @brief Resets the publisher socket 
    /// @return true if success, else false. If mode is not set as a PUBLISHER or PUBLISH_AND_SUBSCRIBE, will return false. 
    bool ResetPublisherSocket()
    {
        std::scoped_lock lock(m_PubMutex);
        if (m_PubSocket)
        {
            m_PubSocket->close();
            LogInfo("Publisher socket reset");
        }
        return InitializePublisherSocket();
    }

    /// @brief Resets the subscriber socket 
    /// @return true if success, else false. If mode is not set as a SUBSCRIBER or PUBLISH_AND_SUBSCRIBE, will return false. 
    bool ResetSubscriberSocket()
    {
        std::scoped_lock lock(m_SubMutex);
        if (m_SubSocket)
        {
            m_SubSocket->close();
            LogInfo("Subscriber socket reset");
        }
        return InitializeSubscriberSocket();
    }

    /// @brief Set or update the error handler callback
    /// @param handler std::function or lambda to be used as a callback
    void SetErrorLogHandler(LogHandler handler)
    {
        m_ErrorHandler = std::move(handler);
    }

    /// @brief Set or update the info handler callback
    /// @param handler std::function or lambda to be used as a callback
    void SetInfoLogHandler(LogHandler handler)
    {
        m_InfoHandler = std::move(handler);
    }

private:
    /// @brief Encodes a topic and message into a single message for transmition.
    /// @param topic - topic for the message
    /// @param message - message to be sent
    /// @return - Encoded message as a uint8_t vector
    std::vector<uint8_t> EncodeMessage(std::string_view topic, std::string_view message)
    {
        std::string combined = std::string(topic) + ":" + std::string(message);
        return { combined.begin(), combined.end() };
    }

    /// @brief Decodes a received message from the subscriber socket. 
    /// @param encodedMessage received encoded message
    /// @return topic, message pair
    std::pair<std::string, std::string> DecodeMessage(const std::vector<uint8_t>& encodedMessage)
    {
        // Convert the vector to a string
        std::string combined(encodedMessage.begin(), encodedMessage.end());

        // Find the delimiter (e.g., ':')
        size_t delimiterPos = combined.find(':');
        if (delimiterPos == std::string::npos) 
        {
            // Return empty strings on invalid format
            return {};
        }

        // Split the string into topic and message
        std::string topic = combined.substr(0, delimiterPos);
        std::string message = combined.substr(delimiterPos + 1);

        return { topic, message };
    }

    /// @brief Logs an error message. By default, prints to std::cerr if user has not passed in a callback 
    /// @param message - Message to be logged
    void LogError(const std::string& errorMessage) 
    {
        if (m_ErrorHandler) 
        {
            m_ErrorHandler(errorMessage);
        }
        else
        {
            std::cerr << "Error: " << errorMessage << std::endl;
        }
    }

    /// @brief Logs an info message. By default, will not print anything. Relies on user passed callback. 
    /// @param message - Message to be logged
    void LogInfo(const std::string& message)
    {
        if (m_InfoHandler)
        {
            m_InfoHandler(message);
        }
    }

    /// @brief Initialzies the punlisher socket for the class
    /// @return true on success, else false
    bool InitializePublisherSocket()
    {
        if (m_PubEndpoints.empty())
        {
            LogError("Publisher socket initialization failed, endpoint list is empty.");
            return false;
        }

        if (!m_Context)
        {
            LogError("Publisher socket initialization failed, context is invalid.");
            return false;
        }

        if (m_Mode != Mode::PUBLISH && m_Mode != Mode::PUBLISH_AND_SUBSCRIBE)
        {
            LogError("Attempt to initialize publisher in unsupported mode");
            return false;
        }

        std::scoped_lock lock(m_PubMutex);
        try
        {
            // Only create the socket if it isn't already initialized
            if (!m_PubSocket)
            {
                m_PubSocket = std::make_unique<zmq::socket_t>(*m_Context, ZMQ_PUB);
            }
            
            for (const auto& endpoint : m_PubEndpoints)
            {
                m_PubSocket->bind(endpoint);
                LogInfo("Publisher bound to endpoint: " + endpoint);
            }

            LogInfo("Publisher socket initialized");
            return true;
        }
        catch (const zmq::error_t& e)
        {
            LogError("Failed to initialize publisher socket in mode " + std::to_string(static_cast<int>(m_Mode)) + ": " + std::string(e.what()));
        }

        return false;
    }

    /// @brief Initialzies the subscriber socket for the class
    /// @return true on success, else false
    bool InitializeSubscriberSocket()
    {
        if (m_SubEndpoints.empty())
        {
            LogError("Subscriber socket initialization failed, endpoint list is empty.");
            return false;
        }

        if (!m_Context)
        {
            LogError("Subscriber socket initialization failed, context is invalid.");
            return false;
        }

        if (m_Mode != Mode::SUBSCRIBE && m_Mode != Mode::PUBLISH_AND_SUBSCRIBE)
        {
            LogError("Attempt to initialize subscriber in unsupported mode");
            return false;
        }

        try
        {
            // Only create the socket if it isn't already initialized
            if (!m_SubSocket)
            {
                m_SubSocket = std::make_unique<zmq::socket_t>(*m_Context, ZMQ_SUB);
            }

            for (const auto& endpoint : m_SubEndpoints)
            {
                m_SubSocket->connect(endpoint);
                LogInfo("Subscriber connected to endpoint: " + endpoint);
            }

            // Subscribe to all messages by default
            m_SubSocket->set(zmq::sockopt::subscribe, "");
            LogInfo("Subscriber socket initialized");
            return true;
        }
        catch (const zmq::error_t& e)
        {
            LogError("Failed to initialize subscriber socket in mode " + std::to_string(static_cast<int>(m_Mode)) + ": " + std::string(e.what()));
        }

        return false;
    }

    /// @brief Close the sockets that are open. 
    /// @brief will also close the context if it is owned by the class. 
    void CloseSockets() 
    {
        try 
        {
            if (m_PubSocket) 
            {
                m_PubSocket->close();
                LogInfo("Publisher socket closed");
            }

            if (m_SubSocket) 
            {
                m_SubSocket->close();
                LogInfo("Subscriber socket closed");
            }

            // Close context only if the class owns it and context is valid
            if (m_OwnContext && m_Context)
            {
                m_Context->close();
                LogInfo("ZMQ context closed");
            }
        }
        catch (const zmq::error_t& e) 
        {
            LogError("Error closing ZMQ context/socket: " + std::string(e.what()));
        }
    }

    /// @brief Initialize the context
    /// @param context - nullptr will tell class to create and handle its own context, passing in a context is also handled for abstract handling.
    void InitializeContext(std::shared_ptr<zmq::context_t> context)
    {
        if (!context)
        {
            m_Context = std::make_shared<zmq::context_t>(1);
            m_OwnContext = true;
        }
        else
        {
            m_Context = context;
            m_OwnContext = false;
        }
    }

    bool m_OwnContext{ false };
    std::shared_ptr<zmq::context_t> m_Context;
    std::unique_ptr<zmq::socket_t> m_PubSocket{ nullptr };
    std::mutex m_PubMutex;
    std::unique_ptr<zmq::socket_t> m_SubSocket{ nullptr };
    std::mutex m_SubMutex;
    Mode m_Mode;
    std::vector<std::string> m_PubEndpoints;
    std::vector<std::string> m_SubEndpoints;
    LogHandler m_ErrorHandler{ nullptr };
    LogHandler m_InfoHandler{ nullptr };
};
