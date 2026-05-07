#pragma once

#include "stream_defs.hpp"
#include "streaming.hpp"
#include "schema.hpp"
#include "utils/serialize.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
namespace quarkbot {

    struct MessageStreamTypeItem: StreamTypeItem {};

    using ConversationID = std::uint64_t;

    ///Message item (for message streams)
    struct Message : StreamTypeItem {
        constexpr static Type type = "ex_message";
        auto &view() {return *this;}

        ///Send of this message
        std::string sender;
        ///Target or topic
        std::string target;
        ///Message payload
        std::vector<std::uint8_t> payload;
        ///Conversation id
        ConversationID conversation_id;
        ///Schema hash of payload
        SchemaHash schema;
        ///time on send side
        std::chrono::system_clock::time_point send_time;

        ///Extract value to type, function checks for schema
        /**
            @param x variable that receives value
            @retval true extracted
            @retval false failed to extract, schema mismatch or parse error
        */
        template<typename T>
        bool extract(T &x) {
            SchemaHash h = get_schema_hash<T>();
            if (h != schema) return false;
            bool valid;
            try {
                srl::BinaryParser parser([&, pos = std::size_t(0)](auto &sp) mutable {
                    if (sp.size() + pos > payload.size()) throw false;
                    std::copy(payload.begin()+pos, sp.begin(), sp.end());
                    pos += sp.size();
                });
                parser(x);
                valid = true;
            } catch (...) {
                valid = false;
            }
            return valid;
        }
        
    };

    ///Class which handles sending and receiving messages
    class IMessageBus {
    public:
        virtual ~IMessageBus() = default;

        ///Subscribe stream
        /**
            @return message stream
        */
        virtual std::unique_ptr<IEventStream<Message> > subscribe() = 0;

        ///Send raw message
        /**
            @param target target or topic
            @param payload
            @param conversation_id id which specifies conversation
            @param schema schema hash of message (optional)
        */
        virtual void send_raw(std::string_view target, std::vector<std::uint8_t> payload, ConversationID conversation_id = {}, SchemaHash schema = {}) = 0;


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
    };



}