#pragma once

#include <quarkbot/mq.h>

#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>
#include "shared/cluster_alloc_reference.h"
namespace quarkbot {



class BasicMQ: public IMQBroker, public std::enable_shared_from_this<BasicMQ> {
public:

    class MessageDef: public IMQBroker::IMessage {
    public:
        MessageDef(std::string_view sender, std::string_view channel, std::string_view message, ConversationID cid, std::shared_ptr<BasicMQ> owner)
            :sender(sender),channel(channel),message(message),cid(cid),owner(owner) {}
        virtual std::string_view get_sender() const override {return sender;}
        virtual std::string_view get_channel() const override {return channel;}
        virtual MessageContent get_content() const override {return message;}
        virtual ConversationID get_conversation() const override {return cid;}

    protected:
        std::string sender;
        std::string channel;
        std::string message;
        ConversationID cid;
        std::shared_ptr<BasicMQ> owner;
    };

    virtual void subscribe(IListener *listener, ChannelID channel) override;
    virtual void unsubscribe(IListener *listener, ChannelID channel) override;
    virtual void unsubscribe_all(IListener *listener) override;
    virtual void send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) override;



protected:

    struct ChanMapItem {
        std::vector<IListener *> _items;
        std::vector<char> _name;
    };


    using ListenerMap = std::unordered_map<IListener *, std::vector<std::string_view>  >;
    using ChannelMap = std::unordered_map<std::string_view, ChanMapItem>;
    ListenerMap _listeners;
    ChannelMap _channels;
    std::unordered_map<IListener *, std::string> _mailboxes_by_ptr;
    std::unordered_map<std::string_view, IListener *> _mailboxes_by_name;
    std::recursive_mutex _mx;

    std::string_view find_mailbox(IListener *lsn) const;

    void remove_listener_from_channel(std::string_view channel, IListener *listener);
    void remove_channel_from_listener(std::string_view channel, IListener *listener);
    void erase_mailbox(IListener *listener);
    std::string_view create_mailbox(IListener *listener);

    std::any _allocator_instance;
};

}
