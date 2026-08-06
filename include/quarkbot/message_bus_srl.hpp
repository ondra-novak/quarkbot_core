/// @file companion header to message_bus.hpp - contains declaration requiring (de)serializer.

#pragma once
#include "message_bus.hpp"
#include "quarkbot/serializer/serialize.hpp"
#include <iterator>

namespace quarkbot {

template<typename T>
inline bool Message::extract(T &x) {
    srl::SchemaHash h = srl::schema_hash<std::decay_t<T> >;
    if (h != schema) return false;
    bool valid;
    try {
        srl::deserialize_from(payload.begin(), payload.end(), x);
        valid = true;
    } catch (...) {
        valid = false;
    }
    return valid;
}
    
template<typename T>
inline void MessageBus::send(std::string_view target, const T &payload, ConversationID conversation_id) {
    std::vector<std::uint8_t> s;
    srl::serialize_to<std::uint8_t>(payload, std::back_inserter(s));
    send_raw(target, std::move(payload), conversation_id, srl::schema_hash<T>());
}

}