#pragma once

#include "abstract/imessage_bus.hpp"
#include "event_stream.hpp"
#include <bit>
#include <cstddef>
namespace quarkbot {


    ///Class which handles sending and receiving messages
    class MessageBus {
    public:
        explicit MessageBus(std::nullptr_t) {}
        MessageBus(std::shared_ptr<IMessageBus> shared):  _shared(std::move(shared)){}

        ///Subscribe stream
        /**
            @return message stream
        */
        EventStream<Message> subscribe() {
            return EventStream<Message>(_shared->subscribe());
        }

        ///Send raw message
        /**
            @param target target or topic
            @param payload
            @param conversation_id id which specifies conversation
            @param schema schema hash of message (optional)
        */
        void send_raw(std::string_view target, std::vector<std::uint8_t> payload, ConversationID conversation_id = {}, SchemaHash schema = {}) {
            _shared->send(target, payload, conversation_id, schema);
        }


        ///Send message
        /**
            @param target target or topic
            @param payload binary payload
            @param conversation_id id which specifies conversation
         */
        template<typename T>
        void send(std::string_view target, const T &payload, ConversationID conversation_id = {}) {
            std::vector<std::uint8_t> s;
            srl::BinarySerializer wr([&](auto &sp){s.insert(s.end(),sp.begin(),sp.end());});
            wr(payload);
            send_raw(target, std::move(payload), conversation_id, get_schema_hash<T>());
        }
    protected:
        std::shared_ptr<IMessageBus> _shared;
    };



}