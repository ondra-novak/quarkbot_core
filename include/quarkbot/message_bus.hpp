#pragma once

#include "abstract/imessage_bus.hpp"
#include "event_stream.hpp"
#include "quarkbot/abstract/default_shared.hpp"
#include <chrono>
#include <cstddef>
namespace quarkbot {


    ///Class which handles sending and receiving messages from and into the strategy
    class MessageBus {
    public:
        MessageBus():_shared(default_shared(null_messagebus)) {}
        MessageBus(std::shared_ptr<IMessageBus> shared):  _shared(std::move(shared)){}

        ///Subscribe stream
        /**
            @return message stream
        */
        EventStream<Message> subscribe() {
            if (_shared) return EventStream<Message>(_shared->subscribe());
            else return {};
        }

        ///Send raw message
        /**
            @param target target or topic
            @param payload
            @param conversation_id id which specifies conversation
            @param schema schema hash of message (optional)
        */
        void send_raw(std::string_view target, std::vector<std::uint8_t> payload, 
                ConversationID conversation_id = {}, 
                srl::SchemaHash schema = {}) {
            _shared->send({
                MessageType::normal_message,
                {},
                std::string(target),
                std::move(payload),
                conversation_id,
                schema,
                std::chrono::system_clock::now(),
            });
             
        }

        void send(const Message &msg) {
            _shared->send(msg);
        }


        ///Send message
        /**
            @param target target or topic
            @param payload binary payload
            @param conversation_id id which specifies conversation
         */
        template<typename T>
        void send(std::string_view target, const T &payload, ConversationID conversation_id = {});
    protected:
        std::shared_ptr<IMessageBus> _shared;
    };



}