#include "storage_msgbus_replicator.hpp"
#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/message_bus.hpp"
#include "quarkbot/persistent.hpp"
#include "quarkbot/storage.hpp"
#include <chrono>
#include <cstdint>

namespace quarkbot {

static std::uint8_t *write_size_2(std::uint8_t *iter, std::size_t sz) {
    if (sz) {
        iter = write_size_2(iter, sz >> 7);
        *iter ++= 0x80 | (sz & 0x7F);
    }
    return iter;
}
static std::uint8_t *write_size(std::uint8_t *iter, std::size_t sz) {
    iter = write_size_2(iter, sz >> 7);
    *iter ++= (sz & 0x7F);
    return iter;
}

static std::uint8_t *write_blob(std::uint8_t *iter, std::string_view data) {
    iter = write_size(iter, data.size());
    iter = std::copy(data.begin(), data.end(), iter);
    return iter;
}

static void replicate_with_buffer(const Storage::ReplicatorEvent &event, std::uint8_t *buffer, MessageBus &bus, const std::string &target) {
    auto *iter = buffer;
    *iter++ = event.erase?'E':'P';
    *iter++ = event.schema_hash?'S':'R';
    iter = write_blob(iter, event.key);
    iter = write_blob(iter, event.value);
    std::size_t final_size = static_cast<std::size_t>(iter - buffer);
    bus.send(Message{
        MessageType::normal_message,{}, target, std::span(buffer, final_size),
        0,0,std::chrono::system_clock::now(),{}        
    });
}

Storage::Replicator::Connection attach_replicator(Storage storage, MessageBus bus, std::string target) {
    return storage.add_replicator(
        [bus, target](const Storage::ReplicatorEvent &event)   mutable noexcept{
            auto needsz = event.key.size()+event.value.size() + 2*((sizeof(std::size_t)*8+6)/7)+2; //reserved space            
            if (needsz > 512) {
                std::vector<std::uint8_t> buffer;
                buffer.resize(needsz);
                replicate_with_buffer(event, buffer.data(), bus, target);
            } else {
                std::uint8_t buffer[512];
                replicate_with_buffer(event, buffer, bus, target);
            }        
    });
}

static std::size_t extract_size(auto &iter, auto end_iter) {
    std::size_t out = 0;
    while (iter != end_iter) {
        auto x = static_cast<std::uint8_t>(*iter++);        
        out = (out << 7) | (x & 0x7F);
        if ((x & 0x80) == 0) return out;
    }
    out = 0;
    return out;
    
}
static std::string_view extract_blob(auto &iter, auto end_iter) {
    std::size_t sz = extract_size(iter, end_iter);
    std::size_t remain = static_cast<std::size_t>(std::distance(iter, end_iter));
    sz = std::min(sz, remain);
    auto end_blob = iter;
    std::advance(end_blob, sz);
    std::span bin(iter, end_blob);
    iter = end_blob;
    return {reinterpret_cast<const char *>(bin.data()), bin.size()};
}

bool replicate_from_message(const std::span<const std::uint8_t> &msg, StorageTransaction &trn) {
    if (msg.size()<4) return false;
    auto iter = msg.begin();
    auto end = msg.end();
    char ch1 = static_cast<char>(*iter++);
    char ch2 = static_cast<char>(*iter++);
    bool erase;
    bool schema;
    if (ch1 == 'E')  erase = true;
    else if (ch2 == 'P') erase = false;
    else return false;

    if (ch2 == 'R') schema = false;
    else if (ch2 == 'S') schema = true;
    else return false;

    std::string_view key = extract_blob(iter, end);
    std::string_view value = extract_blob(iter, end);
    trn.put(Storage::ReplicatorEvent{
        key, value, erase, schema
    });
    return true;
}

StrategyFragment replicate_events(EventStream<Message> msg_stream, Storage storage, std::string filter) {
    Message msg;
    while (co_await msg_stream.receive(msg)) {
        if (filter.empty() || msg.target == filter) {
            auto &trn = shared_transaction(storage);
            replicate_from_message(msg.payload, trn );
        }
    }
}

}
