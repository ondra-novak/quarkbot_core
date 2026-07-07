#pragma once

#include "ieventstream.hpp"

#include "../stream_defs.hpp"
#include "../serializer/schema_fwd.hpp"
#include "quarkbot/log.hpp"
namespace quarkbot {

    struct MessageStreamTypeItem: StreamTypeItem {};

    using ConversationID = std::uint64_t;

    ///Message item (for message streams)
    struct Message : StreamTypeItem {

        ///Send of this message
        std::string sender;
        ///Target or topic
        std::string target;
        ///Message payload
        std::vector<std::uint8_t> payload;
        ///Conversation id
        ConversationID conversation_id;
        ///Schema hash of payload
        srl::SchemaHash schema;
        ///time on send side
        std::chrono::system_clock::time_point send_time;

        ///Extract value to type, function checks for schema
        /**
            @param x variable that receives value
            @retval true extracted
            @retval false failed to extract, schema mismatch or parse error
        */
        template<typename T>
        bool extract(T &x);
    };

    ///Class which handles sending and receiving messages
    class IMessageBus {
    public:
        virtual ~IMessageBus() = default;

        ///Subscribe stream
        /**
            @return message stream
        */
        virtual std::shared_ptr<IEventStream<Message> > subscribe() = 0;

        ///Send raw message
        /**
            @param target target or topic
            @param payload
            @param conversation_id id which specifies conversation
            @param schema schema hash of message (optional)
        */
        virtual void send(std::string_view target, std::vector<std::uint8_t> payload, ConversationID conversation_id = {}, srl::SchemaHash schema = {}) = 0;


        class Null;
    };

    class IMessageBus::Null final: public IMessageBus{
    public:
        virtual std::shared_ptr<IEventStream<Message> > subscribe() override {
            logWarning("Nobody is listening message bus, subscribing to void");
            return IEventStream<Message>::Silent::create_instance();
        }
        virtual void send(std::string_view target, 
            std::vector<std::uint8_t> payload, 
            ConversationID conversation_id, srl::SchemaHash schema) override {
                logWarning("Message discarded: target={}, length={}, conversation_id={}, schema={:x}",
                    target, payload.size(), conversation_id, schema
                );            
        }        
    };

    constexpr auto null_messagebus = IMessageBus::Null();



}