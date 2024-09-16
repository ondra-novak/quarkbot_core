#pragma once
#include <string_view>
#include <memory>
#include <span>


namespace quarkbot {



///abstract interface. See MQBroker for implementation
class IMQBroker {
public:

    ///channels are strings
    using ChannelID = std::string_view;
    ///messages are string
    using MessageContent = std::string_view;
    ///conversation id - using number is enough
    using ConversationID = std::uint32_t;

    ///abstract message
    class IMessage {
    public:
        virtual ChannelID get_sender() const = 0;
        virtual ChannelID get_channel() const = 0;
        virtual MessageContent get_content() const = 0;
        virtual ConversationID get_conversation() const = 0;
        virtual ~IMessage() = default;
    };


    ///message wrapper
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
        ChannelID get_sender() const {return _ptr->get_sender();}
        ///Retrieve channel name
        /**
         * In case that message is private (see pm flag in function on_message), this contains
         * id of your private mailbox. Otherwise it contains channel name
         *  */
        ChannelID get_channel() const {return _ptr->get_channel();}

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

        bool operator==(const Message &other) const = default;

    protected:
        std::shared_ptr<const IMessage> _ptr;
    };

    ///Message listener
    class IListener {
    public:
        virtual ~IListener() = default;
        ///Message received
        /**
         * @param message contains message
         * @param pm this is set to true, if message was sent to private mailbox.
         * If this is set false, the message was sent to public channel.
         *
         * @note if pm is set, channel name of the message is undefined
         */
        virtual void on_message(const Message &message, bool pm) noexcept= 0;
    };

    ///Broker monitoring
    /** This class is important to implement forwarding nodes */
    class IMonitor {
    public:
        virtual ~IMonitor() = default;
        ///notifies that list of channels has been updated
        /** Called under lock. You should send a notify to a processing thread
         *  to broadcast a new list of channels. You can use get_active_channels in that thread */
        virtual void on_channels_update() noexcept = 0;
        ///notifieas that a message to a channel has been dropped because channel doesn't exist
        /** Called under lock. You can send a message to a peer, that nobody is listening
         * @param lsn sender (listener) - can be nullptr
         * @param msg message
         * @retval true message has been processed
         * @retval false message was not processed (indicate to caller)
         *
         * */
        virtual bool on_message_dropped(IListener *lsn, const Message &msg) noexcept = 0;
    };

    ///Listener which receives current channel list
    class IChannelListCallback {
    public:
        virtual ~IChannelListCallback() = default;
        virtual void operator()(std::span<ChannelID> channels) = 0;
    };

    ///subscribe channel
    /**
     * @param listener listener of messages
     * @param channel channel
     */
    virtual void subscribe(IListener *listener, ChannelID channel) = 0;
    ///unsubscribe channel
    /**
     * @param listener listene to unsubscribe
     * @param channel
     */
    virtual void unsubscribe(IListener *listener, ChannelID channel) = 0;
    ///unsubscribe listener from all channels
    /**
     * @param listener listener
     * after return, the associated object can be destroyed
     */
    virtual void unsubscribe_all(IListener *listener) = 0;

    ///unsubscribe private channel
    /**
     * This closes private channel and thus prevents to receive more private messages
     *
     * Private channel is created automatically when you specify a listener as
     * a parameter of the function send_message. This function deletes this
     * channel. Note that next usage of send_message creates new private channel
     * with a different address
     *
     * @param listener owner of a private channel.
     */
    virtual void unsubcribe_private(IListener *listener) = 0;
    ///send message
    /**
     * @param listener sender's listener. can be nullptr to send anonymous message
     * @param channel channel
     * @param msg message
     * @param cid conversation identifier, can be 0 if has no meaning
     * @retval true message has been posted (it doesn't indicate that has been delivered)
     * @retval false message was not posted (no information about how to route message)
     */
    virtual bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) = 0;
    virtual ~IMQBroker() = default;

    //proxy features
    /// forward message from one hop to other
    /**
     * @param listener identification of who forwarding this message.
     * @param msg message to forward. This message should be received by broker of other node
     * @param subscribe_return_path true value in this field causes that listener is also registered
     * as return path. This is similar to subscribe on sender, however the broker updates return path
     * for existing sender instead registering additional paths (as there should be one path per sender).
     * By setting this flag, the listener is attached to the broker. To detach, you need to call unsubscribe_all()
     * This flag is ignored if listener is nullptr
    *
     * @retval true message has been posted (it doesn't indicate that has been delivered)
     * @retval false message was not posted (no information about how to route message)
     *
     */
    virtual bool forward_message(IListener *listener, const Message &msg, bool subscribe_return_path) = 0;

    ///Creates a message
    /** Useful to convert network representation of the message to Message object */

    virtual Message create_message(ChannelID sender, ChannelID channel, MessageContent msg, ConversationID cid) = 0;

    ///Generate random channel name
    /**
     * @param prefix channel name prefix
     * @return a random channel name with high entropy.
     *
     * @note useful to create ad-hoc multicast groups.
     */
    virtual std::string create_random_channel_name(std::string_view prefix) const = 0;

    ///Register broker monitor
    virtual void register_monitor(IMonitor *mon) = 0;
    ///Unregister broker monitor
    virtual void unregister_monitor(const IMonitor *mon) = 0;

    ///Retrieves list of active channels subscribed by other listeners
    /**
     * @param listener specifies listener. Channels subscribed by this listener are
     * skipped/
     * @param cb callback object, which receives list
     *
     * @note the callback is called under a lock. (this is reasony, why callback is used)
     */
    virtual void get_active_channels(IListener *listener, IChannelListCallback &&cb) const = 0;

    ///Retrieves list of subscribed channels for given listener
    /**
     * @param listener listener
     * @param cb callback
     */
    virtual void get_subscribed_channels(IListener *listener, IChannelListCallback &&cb) const = 0;

    ///Determines whether given name is channel
    /**
     * @retval true id found as channel
     * @retval false id was not found (not exist or can be mailbox)
     */
    virtual bool is_channel(ChannelID id) const = 0;
};

///Wraps IMQBroker to single class which can be further extended
class MQBroker {
public:

    using ChannelID = IMQBroker::ChannelID;
    using Message = IMQBroker::Message;
    using MessageContent = IMQBroker::MessageContent;
    using IListener = IMQBroker::IListener;
    using IMonitor = IMQBroker::IMonitor;
    using ConversationID = IMQBroker::ConversationID;


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

    ///unsubscribe private channel
    /**
     * This closes private channel and thus prevents to receive more private messages
     *
     * Private channel is created automatically when you specify a listener as
     * a parameter of the function send_message. This function deletes this
     * channel. Note that next usage of send_message creates new private channel
     * with a different address
     *
     * @param listener owner of a private channel.
     */
    void unsubcribe_private(IListener *listener){
        _ptr->unsubcribe_private(listener);
    }

    ///Generate random channel name
    /**
     * @param prefix channel name prefix
     * @return a random channel name with high entropy.
     *
     * @note useful to create ad-hoc multicast groups.
     */
    virtual std::string create_random_channel_name(std::string_view prefix) const {
        return _ptr->create_random_channel_name(prefix);
    }
    ///send message
    /**
     * @param listener a listener with mailbox, can be nullptr. If this pointer is nullptr,
     * then message will be send without sender's id. (will be empty)
     * @param channel target channel - must not be empty
     * @param message message to send
     * @param cid a conversation ID. This number helps to distinguish conversation
     * on the same channel. Can be zero if has no meaning
     *
     * @note function subscribes local mailbox for the first time of call with new listener.
     * You need to unsubscribe_all() before listener is destroyed.
     */
    bool send_message(IListener *listener, ChannelID channel, MessageContent message, ConversationID cid = 0) {
        return _ptr->send_message(listener, channel, message, cid);
    }


    ///retrieve active channels
    /**
     *
     * @param listener listener to define perspective (removes channels subscribed by
     * this listener). If you need all channels, specify nullptr
     * @param cb callback function which receives a span of channels
     */
    template<std::invocable<std::span<ChannelID> > Callback>
    void get_active_channels(IListener *listener, Callback &&cb) {
        _ptr->get_active_channels(listener, ChannelListCbWrapper<Callback>(std::forward<Callback>(cb)));
    }


    ///retrieve subscribed channels for listener
    /**
     *
     * @param listener
     * @param cb callback function which receives a span of channels
     */
    template<std::invocable<std::span<ChannelID> > Callback>
    void get_subscribed_channels(IListener *listener, Callback &&cb) {
        _ptr->get_subscribed_channels(listener, ChannelListCbWrapper<Callback>(std::forward<Callback>(cb)));
    }

    ///retrieves pointer implementation object
    auto get_handle() const {return _ptr;}


    ///Create message
    /**
     * @param sender sender
     * @param channel channel
     * @param msg message
     * @param cid conversation id
     * @return message instance
     */
    Message create_message(ChannelID sender, ChannelID channel, MessageContent msg, ConversationID cid) {
        return _ptr->create_message(sender, channel, msg, cid);
    }

    //proxy features
    /// forward message from one hop to other
    /**
     * @param listener identification of who forwarding this message.
     * @param msg message to forward. This message should be received by broker of other node
     * @param subscribe_return_path true value in this field causes that listener is also registered
     * as return path. This is similar to subscribe on sender, however the broker updates return path
     * for existing sender instead registering additional paths (as there should be one path per sender).
     * By setting this flag, the listener is attached to the broker. To detach, you need to call unsubscribe_all()
     * This flag is ignored if listener is nullptr
     *
     * @retval true message has been posted (it doesn't indicate that has been delivered)
     * @retval false message was not posted (no information about how to route message)

     */
    bool forward_message(IListener *listener, Message msg, bool subscribe_return_path) {
        return _ptr->forward_message(listener, std::move(msg), subscribe_return_path);
    }

    ///Register broker monitor
    /**
     * @param mon monitoring object
     */
    void register_monitor(IMonitor *mon) {
        _ptr->register_monitor(mon);
    }
    ///Unregister broker monitor
    /**
     * @param mon monitoring object
     */
    void unregister_monitor(const IMonitor *mon) {
        _ptr->unregister_monitor(mon);
    }

    virtual bool is_channel(ChannelID id) const {
        return _ptr->is_channel(id);
    }
protected:
    std::shared_ptr<IMQBroker> _ptr;

    template<typename Callback>
    class ChannelListCbWrapper: public IMQBroker::IChannelListCallback {
    public:
        ChannelListCbWrapper(Callback &&_cb):__cb(std::forward<Callback>(_cb)) {}
        void operator()(std::span<ChannelID> channels) {
            std::forward<Callback>(__cb)(channels);
        }
    protected:
        Callback &&__cb;
    };


};

///RAII client implementation
/**
 * Automatically unsubscribe in destructor
 * @tparam invocable class, which can be called with two arguments. A message
 * and boolean flag, which specifies, whether message is private(true) or public(false).
 * This function can optionally return true to keep subscribed and false to unsubscribe
 * self from the current channel (ignored in case that pm is true, as you cannot unsubscribe
 * from your mailbox).
 * @note boolean flag pm is optional. The function can also accept only one argument. This
 * is useful for clients, which doesn't listening public channels, so all messages
 * are private
 * @see MQBroker::IListener
 */
class MQClient: public MQBroker::IListener  {
public:


    using ChannelID = MQBroker::ChannelID;
    using MessageContent = MQBroker::MessageContent;
    using Message = MQBroker::Message;
    using ConversationID = MQBroker::ConversationID;


    ///Initialize the client
    /**
     * @param broker broker instance
     * @param listener pointer to an instance, which receives messages
     */
    MQClient(MQBroker broker)
        :_broker(broker) {}
    ~MQClient() {
        _broker.unsubscribe_all(this);
    }

    MQClient(const MQClient &other) = delete;
    MQClient &operator=(const MQClient &other) = delete;

    ///Subscribe the channel
    /** This allows to receive messages sent by other clients to this channel
     * @param listener listener object
     * @param channel channel name. The channel name must not be empty
     * @note it is not error to call this function for channel which is already subscribed.
     * In this case, the channel stays subscribed
     */
    void subscribe(ChannelID channel) {
        _broker.subscribe(this, channel);
    }
    ///Unsubscribe the channel
    /**
     * @param listener subscribed listener
     * @param channel channel
     * @note it is not error to call this function for channel which is not subscribed
     */
    void unsubscribe( ChannelID channel){
        _broker.unsubscribe(this, channel);
    }
    ///Unsubscribe from all channels
    /**
     * @param listener listener to unsubscribe
     * @note after all unsubscribed, you can destroy the listener
     */
    void unsubscribe_all() {
        _broker.unsubscribe_all(this);
    }

    void unsubscribe_private() {
        _broker.unsubcribe_private(this);
    }

    ///send message
    /**
     * @param channel target channel
     * @param message message to send
     * @note sending message to an empty named channel always drops the message
     */
    void send_message(ChannelID channel, MessageContent message, ConversationID cid = 0) {
        _broker.send_message(this, channel, message, cid);
    }


    ///access broker instance
    IMQBroker *operator->() const {
        return _broker.get_handle().get();
    }

    ///access broker instance
    MQBroker get_broker() const {
        return _broker;
    }

    ///create MQClient instance which calls a callback with incoming message
    /**
     * @param broker broker instance
     * @param fn a callback function
     * @return an instance. You can store it in local variable (use auto) or you
     * can use new auto() to allocate dynamically
     */
    template<typename Fn>
    requires((std::is_invocable_v<Fn, MQClient &, const MQBroker::Message&, bool >
        || std::is_invocable_v<Fn, MQClient &,const MQBroker::Message&>))
    static auto create(MQBroker broker, Fn &&fn);

    template<typename Fn>
    requires((std::is_invocable_v<Fn, MQClient &, const MQBroker::Message&, bool >
        || std::is_invocable_v<Fn, MQClient &,const MQBroker::Message&>))
    static std::unique_ptr<MQClient> make_unique(MQBroker broker, Fn &&fn);

    template<typename Fn>
    requires((std::is_invocable_v<Fn, MQClient &, const MQBroker::Message&, bool >
        || std::is_invocable_v<Fn, MQClient &,const MQBroker::Message&>))
    static std::shared_ptr<MQClient> make_shared(MQBroker broker, Fn &&fn);


protected:
    MQBroker _broker;

};


template<typename Fn>
requires((std::is_invocable_v<Fn, MQClient &, const MQBroker::Message&, bool >
    || std::is_invocable_v<Fn, MQClient &,const MQBroker::Message&>))
class MQClientCB: public MQClient {
public:

    MQClientCB(MQBroker broker, Fn &&fn):
        MQClient(std::move(broker)), _fn(std::forward<Fn>(fn)) {}

protected:
    Fn _fn;

    virtual void on_message(const Message &msg, bool pm) noexcept override {
        if constexpr(std::is_invocable_v<Fn, MQClient &, const Message &>) {
            using T = decltype(_fn(*this, msg));
            if constexpr (std::is_convertible_v<T, bool>) {
                bool r = _fn(*this, msg);
                if (!pm && !r) _broker.unsubscribe(this, msg.get_channel());
            } else {
                _fn(*this,msg);
            }
        } else {
            using T = decltype(_fn(*this, msg,pm));
            if constexpr (std::is_convertible_v<T, bool>) {
                bool r = _fn(*this,msg,pm);
                if (!pm && !r) _broker.unsubscribe(this, msg.get_channel());
            } else {
                _fn(*this, msg, pm);
            }
        }
    }
};



template<typename Fn>
requires((std::is_invocable_v<Fn, MQClient &, const MQBroker::Message&, bool >
    || std::is_invocable_v<Fn, MQClient &,const MQBroker::Message&>))
auto MQClient::create(MQBroker broker, Fn &&fn) {
    return MQClientCB<Fn>(std::move(broker),std::forward<Fn>(fn));
}

template<typename Fn>
requires((std::is_invocable_v<Fn, MQClient &, const MQBroker::Message&, bool >
    || std::is_invocable_v<Fn, MQClient &,const MQBroker::Message&>))
std::unique_ptr<MQClient> MQClient::make_unique(MQBroker broker, Fn &&fn) {
    return std::make_unique<MQClientCB<Fn> >(std::move(broker),std::forward<Fn>(fn));
}

template<typename Fn>
requires((std::is_invocable_v<Fn, MQClient &, const MQBroker::Message&, bool >
    || std::is_invocable_v<Fn, MQClient &,const MQBroker::Message&>))
std::shared_ptr<MQClient> MQClient::make_shared(MQBroker broker, Fn &&fn) {
    return std::make_shared<MQClientCB<Fn> >(std::move(broker),std::forward<Fn>(fn));
}



}
