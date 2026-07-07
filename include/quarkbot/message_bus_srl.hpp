/// @file companion header to message_bus.hpp - contains declaration requiring (de)serializer.

#pragma once
#include "message_bus.hpp"
#include "quarkbot/serializer/serialize.hpp"

namespace quarkbot {

template<typename T>
inline bool Message::extract(T &x) {
    srl::SchemaHash h = get_schema_hash<T>();
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
    
template<typename T>
inline void MessageBus::send(std::string_view target, const T &payload, ConversationID conversation_id) {
    std::vector<std::uint8_t> s;
    srl::BinarySerializer wr([&](auto &sp){s.insert(s.end(),sp.begin(),sp.end());});
    wr(payload);
    send_raw(target, std::move(payload), conversation_id, get_schema_hash<T>());
}


}