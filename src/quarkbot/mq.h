#pragma once
#include <memory>
#include "common.h"

namespace quarkbot {

class IMQBroker {
public:


    using ChannelID = std::string_view;
    using MessageContent = std::string_view;
    using ConversationID = std::uint32_t;

    class IMessage {
    public:
        virtual std::string_view get_sender() const = 0;
        virtual std::string_view get_channel() const = 0;
        virtual MessageContent get_content() const = 0;
        virtual ConversationID get_conversation() const = 0;
        virtual ~IMessage() = default;
    };


    class Message {
    public:
        Message(const std::shared_ptr<const IMessage> &ptr):_ptr(ptr) {}
        ///Retrieve sender
        /**
         * @return retrieves sender's mailbox address. If you need to send a response to
         * the sender directly, you simply use this address as channel.
         *
         * Every listener subscribes to its local mailbox by sending a message for the first
         * time
         */
        std::string_view get_sender() const {return _ptr->get_sender();}
        ///Retrieve channel name
        /**
         * @return name of channel, where the message was posted. If the message was posted
         * to private channel, this function returns empty string
         *  */
        std::string_view get_channel() const {return _ptr->get_channel();}

        /**Retrieve message content
         * @return message content
         */
        MessageContent get_content() const {return _ptr->get_content();}

        ///Retrieve conversation ID
        /**
         * Conversation ID is an arbitrary number which can help to manage multiple
         * conversations especially in private messages. This number is
         * often sent with message to help identify conversation where this message
         * belongs. If used in channels, it is often used to provide a new
         * conversation ID for following private conversation.
         *
         * However, in general, this is just a number carried with the message.
         * Default value is zero.
         *
         * @return conversation ID of the message
         */
        ConversationID get_conversation() const {return _ptr->get_conversation();}

    protected:
        std::shared_ptr<const IMessage> _ptr;
    };

    class IListener {
    public:
        virtual ~IListener() = default;
        virtual void on_message(Message message) = 0;
    };

    virtual void subscribe(IListener *listener, ChannelID channel) = 0;
    virtual void unsubscribe(IListener *listener, ChannelID channel) = 0;
    virtual void unsubscribe_all(IListener *listener) = 0;
    virtual void send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) = 0;
    virtual ~IMQBroker() = default;

    class Null;
};

class IMQBroker::Null: public IMQBroker {
public:
    virtual void subscribe(IListener *, ChannelID ) override {}
    virtual void unsubscribe(IListener *, ChannelID ) override {}
    virtual void unsubscribe_all(IListener *) override {}
    virtual void send_message(IListener *, ChannelID, MessageContent, ConversationID ) override {}
};

class MQBroker {
public:

    using ChannelID = IMQBroker::ChannelID;
    using Message = IMQBroker::Message;
    using MessageContent = IMQBroker::MessageContent;
    using IListener = IMQBroker::IListener;
    using ConversationID = IMQBroker::ConversationID;

    static constexpr IMQBroker::Null null_broker = {};

    MQBroker() {
        static std::shared_ptr<IMQBroker> null_ptr(const_cast<IMQBroker::Null *>(&null_broker),[](auto){});
        _ptr = null_ptr;
    }
    MQBroker(std::shared_ptr<IMQBroker> ptr):_ptr(std::move(ptr)) {}


    ///Subscribe the channel
    /** This allows to receive messages sent by other clients to this channel
     * @param listener listener object
     * @param channel channel name. The channel name must not be empty
     * @note there is no way to check, that channel exists or if there is someone
     * listening the channel. Messages sent to non-existing channels are lost
     * @note it is not error to call this function for channel which is already subscribed.
     * In this case, the channel stays subscribed
     */
    void subscribe(IListener *listener, ChannelID channel) {
        _ptr->subscribe(listener, channel);
    }
    ///Unsubscribe the channel
    /**
     * @param listener subscribed listener
     * @param channel channel
     * @note it is not error to call this function for channel which is not subscribed
     */
    void unsubscribe(IListener *listener, ChannelID channel){
        _ptr->unsubscribe(listener, channel);
    }
    ///Unsubscribe from all channels
    /**
     * @param listener listener to unsubscribe
     * @note after all unsubscribed, you can destroy the listener
     */
    void unsubscribe_all(IListener *listener) {
        _ptr->unsubscribe_all(listener);
    }

    ///send message
    /**
     * @param listener a listener with mailbox, can be nullptr. If this pointer is null,
     * then message will be send without sender's id. (will be empty)
     * @param channel target channel
     * @param message message to send
     * @param cid a conversation ID. This number helps to distinguish conversation
     * on the same channel. Actual value has no specific meaning, only to identify
     * topic or conversation in the channel.
     *
     * @note sending message to an empty named channel always drops the message
     *
     * @note function subscribes local mailbox for the first time of call with new listener.
     * You need to unsubscribe_all() before listener is destroyed. If you don't
     * want to manage a listener instance, you can pass nullptr as listener. In this
     * case, you cannot receive any response.
     */
    void send_message(IListener *listener, ChannelID channel, MessageContent message, ConversationID cid = 0) {
        _ptr->send_message(listener, channel, message, cid);
    }

    auto get_handle() const {return _ptr;}

    explicit operator bool() const {return _ptr.get() != &null_broker;}
    bool defined() const {return _ptr.get() != &null_broker;}
    bool operator!() const {return _ptr.get() == &null_broker;}


protected:
    std::shared_ptr<IMQBroker> _ptr;
};

class MQClient {
public:

    using ChannelID = MQBroker::ChannelID;
    using MessageContent = MQBroker::MessageContent;
    using Message = MQBroker::Message;
    using ConversationID = MQBroker::ConversationID;

    MQClient(MQBroker broker, MQBroker::IListener *listener)
        :_broker(broker),_listener(listener) {   }
    ~MQClient() {_broker.unsubscribe_all(_listener);}


    ///Subscribe the channel
    /** This allows to receive messages sent by other clients to this channel
     * @param listener listener object
     * @param channel channel name. The channel name must not be empty
     * @note there is no way to check, that channel exists or if there is someone
     * listening the channel. Messages sent to non-existing channels are lost
     * @note it is not error to call this function for channel which is already subscribed.
     * In this case, the channel stays subscribed
     */
    void subscribe(ChannelID channel) {
        _broker.subscribe(_listener, channel);
    }
    ///Unsubscribe the channel
    /**
     * @param listener subscribed listener
     * @param channel channel
     * @note it is not error to call this function for channel which is not subscribed
     */
    void unsubscribe( ChannelID channel){
        _broker.unsubscribe(_listener, channel);
    }
    ///Unsubscribe from all channels
    /**
     * @param listener listener to unsubscribe
     * @note after all unsubscribed, you can destroy the listener
     */
    void unsubscribe_all() {
        _broker.unsubscribe_all(_listener);
    }

    ///send message
    /**
     * @param channel target channel
     * @param message message to send
     * @note sending message to an empty named channel always drops the message
     */
    void send_message(ChannelID channel, MessageContent message, ConversationID cid = 0) {
        _broker.send_message(_listener, channel, message, cid);
    }

protected:
    MQBroker _broker;
    MQBroker::IListener *_listener;
};



}
