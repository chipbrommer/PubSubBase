#pragma once

#include <zmq.hpp>
#include <string>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <mutex>

class PubSubBase 
{
public:
    enum class Mode { PUBLISH, SUBSCRIBE, PUBLISH_AND_SUBSCRIBE};

    using LogHandler = std::function<void(const std::string&)>;

    PubSubBase(Mode mode, const std::string& pubEndpoint, const std::string& subEndpoint = "",
        LogHandler errorHandler = [](const std::string&) {},
        LogHandler infoHandler = [](const std::string&) {},
        std::shared_ptr<zmq::context_t> context = nullptr)
        : m_Mode(mode), m_PubEndpoint(pubEndpoint), m_SubEndpoint(subEndpoint),
        m_ErrorHandler(std::move(errorHandler)), m_InfoHandler(std::move(infoHandler))
    {
        InitializeContext(context);
        InitializePublisherSocket();
        InitializeSubscriberSocket();
    }

    ~PubSubBase() 
    {
        CloseSockets();
    }

    PubSubBase(const PubSubBase&) = delete;
    PubSubBase& operator=(const PubSubBase&) = delete;
    PubSubBase(PubSubBase&&) = delete;
    PubSubBase& operator=(PubSubBase&&) = delete;

    bool IsValid() const 
    {
        return !m_PubEndpoint.empty() && (m_Mode == Mode::PUBLISH || !m_SubEndpoint.empty());
    }

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

    // Updates the subscribed to topics
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

    // Method to send messages (used only in PUBLISH mode)
    [[nodiscard]] bool SendMessage(std::string_view topic, std::string_view message)
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

        try
        {
            zmq::message_t topic_msg(topic.data(), topic.size());
            zmq::message_t data_msg(message.data(), message.size());

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

    [[nodiscard]] bool SendMessageWithRetry(std::string_view topic, std::string_view message, int retries = 3)
    {
        while (retries-- > 0)
        {
            if (SendMessage(topic, message))
            {
                return true;
            }
            LogError("Retrying message send...");
        }
        return false;
    }

    // Method to receive messages (used only in SUBSCRIBE mode)
    [[nodiscard]] bool ReceiveMessage(std::string& topic, std::string& message, bool blocking = false)
    {
        std::scoped_lock lock(m_SubMutex);
        if (!m_SubSocket)
        {
            LogError("ReceiveMessage() called but no subscriber socket initialized");
            return false;
        }

        try
        {
            zmq::recv_flags flags = blocking ? zmq::recv_flags::none : zmq::recv_flags::dontwait;

            zmq::message_t topic_msg, data_msg;
            if (m_SubSocket->recv(topic_msg, flags) && 
                m_SubSocket->recv(data_msg, zmq::recv_flags::none))
            {
                topic.assign(static_cast<char*>(topic_msg.data()), topic_msg.size());
                message.assign(static_cast<char*>(data_msg.data()), data_msg.size());
                return true;
            }
        }
        catch (const zmq::error_t& e)
        {
            LogError("Error receiving message: " + std::string(e.what()));
        }

        return false;
    }

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

    // Set or update the error handler callback
    void SetErrorLogHandler(LogHandler handler)
    {
        m_ErrorHandler = std::move(handler);
    }

    // Set or update the error handler callback
    void SetInfoLogHandler(LogHandler handler)
    {
        m_InfoHandler = std::move(handler);
    }

private:
    std::vector<uint8_t> EncodeMessage(std::string_view topic, std::string_view message)
    {
        std::string combined = std::string(topic) + ":" + std::string(message);
        return { combined.begin(), combined.end() };
    }

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

    void LogInfo(const std::string& message)
    {
        if (m_InfoHandler)
        {
            m_InfoHandler(message);
        }
    }

    bool InitializePublisherSocket()
    {
        if (m_PubEndpoint.empty())
        {
            LogError("Publisher socket initialization failed, endpoint is empty.");
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
            m_PubSocket->bind(m_PubEndpoint);
            LogInfo("Publisher socket initialized and bound to: " + m_PubEndpoint);
            return true;
        }
        catch (const zmq::error_t& e)
        {
            LogError("Failed to initialize publisher socket in mode " + std::to_string(static_cast<int>(m_Mode)) + ": " + std::string(e.what()));
        }

        return false;
    }

    bool InitializeSubscriberSocket()
    {
        if (m_SubEndpoint.empty())
        {
            LogError("Subscriber socket initialization failed, endpoint is empty.");
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
            m_SubSocket->connect(m_SubEndpoint);

            // Subscribe to all messages by default
            m_SubSocket->set(zmq::sockopt::subscribe, "");
            LogInfo("Subscriber socket initialized and connected to: " + m_SubEndpoint);
            return true;
        }
        catch (const zmq::error_t& e)
        {
            LogError("Failed to initialize subscriber socket in mode " + std::to_string(static_cast<int>(m_Mode)) + ": " + std::string(e.what()));
        }

        return false;
    }

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
    std::string m_PubEndpoint;
    std::string m_SubEndpoint;
    LogHandler m_ErrorHandler{ nullptr };
    LogHandler m_InfoHandler{ nullptr };
};
