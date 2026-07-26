#pragma once

#include "abstract/imessage_bus.hpp"
#include "event_stream.hpp"
#include "utils/wrapper.hpp"
#include <chrono>
#include <cstddef>
namespace quarkbot {


    ///Class which handles sending and receiving messages from and into the strategy
    class MessageBus: public Wrapper<IMessageBus> {
    public:
        
        using Wrapper<IMessageBus>::Wrapper;

        ///Subscribe stream
        /**
            @return message stream
        */
        EventStream<Message> subscribe() {
            if (_ptr) return EventStream<Message>(_ptr->subscribe());
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
            _ptr->send({
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
            _ptr->send(msg);
        }


        ///Send message
        /**
            @param target target or topic
            @param payload binary payload
            @param conversation_id id which specifies conversation
         */
        template<typename T>
        void send(std::string_view target, const T &payload, ConversationID conversation_id = {});
        
    };



}