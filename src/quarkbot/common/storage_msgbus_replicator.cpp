#include "storage_msgbus_replicator.hpp"
#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/message_bus.hpp"
#include "quarkbot/persistent.hpp"
#include "quarkbot/storage.hpp"
#include "quarkbot/utils/bigendian.hpp"
#include "storage_common.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

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

constexpr std::string_view repl_content_type = "application/prs.db-repl.command.v2";

///one type byte, a 16 byte recordkey and two variable length sizes
static constexpr std::size_t repl_fixed_overhead = 1 + recordkey_string_size
        + 2 * ((sizeof(std::size_t)*8+6)/7);

static char type_to_wire(Storage::ReplicatorEvent::Type t) {
    using Type = Storage::ReplicatorEvent::Type;
    switch (t) {
        case Type::put_schema: return 'S';
        case Type::put_key_value: return 'P';
        case Type::update_latest: return 'L';
        case Type::erase_key: return 'K';
        case Type::erase_latest: return 'R';
        case Type::erase_name: return 'N';
    }
    return 0;
}

//the schema-hash term covers put_schema, whose 8-byte fixed field is not part of
//repl_fixed_overhead - every other type leaves it unused
std::size_t replication_message_size(const Storage::ReplicatorEvent &ev) {
    return repl_fixed_overhead + ev.name.size() + ev.value.size() + sizeof(srl::SchemaHash);
}

std::span<char> encode_replication_message(const Storage::ReplicatorEvent &ev, std::span<char> buffer) {
    using Type = Storage::ReplicatorEvent::Type;
    assert(buffer.size() >= replication_message_size(ev));
    auto *begin = reinterpret_cast<std::uint8_t *>(buffer.data());
    auto *iter = begin;
    *iter++ = static_cast<std::uint8_t>(type_to_wire(ev.type));
    switch (ev.type) {
        case Type::put_schema:
            //big_endian_binarize writes through a char iterator; iter here is uint8_t*
            //(write_blob's currency), so hand it a char* view of the same bytes
            big_endian_binarize(ev.schema_hash, reinterpret_cast<char *>(iter));
            iter += sizeof(srl::SchemaHash);
            iter = write_blob(iter, ev.value);
            break;
        case Type::put_key_value: {
            iter = write_blob(iter, ev.name);
            auto rw = record_key_to_string(ev.recordkey);
            iter = std::copy(rw.begin(), rw.end(), iter);
            iter = write_blob(iter, ev.value);
        } break;
        case Type::update_latest:
        case Type::erase_key: {
            iter = write_blob(iter, ev.name);
            auto rw = record_key_to_string(ev.recordkey);
            iter = std::copy(rw.begin(), rw.end(), iter);
        } break;
        case Type::erase_latest:
        case Type::erase_name:
            iter = write_blob(iter, ev.name);
            break;
    }
    return buffer.subspan(0, static_cast<std::size_t>(iter - begin));
}

Storage::Replicator::Connection attach_replicator(Storage storage, MessageBus bus, std::string target) {
    return storage.add_replicator(
        [bus, target](const Storage::ReplicatorEvent &event) mutable noexcept{
            auto send = [&](std::span<char> buffer){
                auto msg = encode_replication_message(event, buffer);
                bus.send(Message{
                    MessageType::normal_message,{}, target, {msg.data(), msg.size()},
                    repl_content_type,0,std::chrono::system_clock::now(),{}
                });
            };
            auto needsz = replication_message_size(event);
            if (needsz > 512) {
                std::vector<char> buffer(needsz);
                send(buffer);
            } else {
                char buffer[512];
                send(buffer);
            }
    });
}

///reads a variable length size, or nothing when the message ends mid-encoding (a
///genuine zero is a single byte with the continuation bit clear, so it is never
///confused with running off the end before that terminating byte is seen)
static std::optional<std::size_t> extract_size(auto &iter, auto end_iter) {
    std::size_t out = 0;
    while (iter != end_iter) {
        auto x = static_cast<std::uint8_t>(*iter++);
        out = (out << 7) | (x & 0x7F);
        if ((x & 0x80) == 0) return out;
    }
    return std::nullopt;
}
///reads a blob, or nothing when its length prefix is truncated; a zero-length blob
///is legitimate and still returns a (valid, empty) string_view
static std::optional<std::string_view> extract_blob(auto &iter, auto end_iter) {
    auto sz = extract_size(iter, end_iter);
    if (!sz) return std::nullopt;
    std::size_t remain = static_cast<std::size_t>(std::distance(iter, end_iter));
    std::size_t n = std::min(*sz, remain);
    auto end_blob = iter;
    std::advance(end_blob, n);
    std::span bin(iter, end_blob);
    iter = end_blob;
    return std::string_view(reinterpret_cast<const char *>(bin.data()), bin.size());
}

///reads a fixed count of bytes, or returns nothing when the message is too short
static std::optional<std::string_view> extract_fixed(auto &iter, auto end_iter, std::size_t count) {
    if (static_cast<std::size_t>(std::distance(iter, end_iter)) < count) return {};
    auto begin = iter;
    std::advance(iter, count);
    return std::string_view(&*begin, count);
}

bool replicate_from_message(const std::string_view &msg, StorageTransaction &trn) {
    using Type = Storage::ReplicatorEvent::Type;
    if (msg.empty()) return false;
    auto iter = msg.begin();
    auto end = msg.end();
    char t = *iter++;

    //a blob length is clamped to the rest of the message, but a fixed width field cut
    //short would otherwise yield a RecordKey built from arbitrary bytes
    auto read_recordkey = [&](RecordKey &out) {
        auto bin = extract_fixed(iter, end, recordkey_string_size);
        if (!bin) return false;
        out = string_to_record_key(*bin);
        return true;
    };
    //a truncated length prefix must be refused rather than read as an empty blob -
    //a zero-length blob still round-trips fine since it is a genuine, complete size
    auto read_blob = [&](std::string_view &out) {
        auto bin = extract_blob(iter, end);
        if (!bin) return false;
        out = *bin;
        return true;
    };

    Storage::ReplicatorEvent ev{.type = Type::put_key_value};
    switch (t) {
        case 'S': {
            auto bin = extract_fixed(iter, end, sizeof(srl::SchemaHash));
            if (!bin) return false;
            ev.type = Type::put_schema;
            big_endian_unbinarize(ev.schema_hash, bin->begin());
            if (!read_blob(ev.value)) return false;
        } break;
        case 'P':
            ev.type = Type::put_key_value;
            if (!read_blob(ev.name)) return false;
            if (!read_recordkey(ev.recordkey)) return false;
            if (!read_blob(ev.value)) return false;
            break;
        case 'L':
        case 'K':
            ev.type = t == 'L'?Type::update_latest:Type::erase_key;
            if (!read_blob(ev.name)) return false;
            if (!read_recordkey(ev.recordkey)) return false;
            break;
        case 'R':
        case 'N':
            ev.type = t == 'R'?Type::erase_latest:Type::erase_name;
            if (!read_blob(ev.name)) return false;
            break;
        default:
            return false;
    }
    trn.apply(ev);
    return true;
}

StrategyFragment replicate_events(EventStream<Message> msg_stream, Storage storage, std::string filter) {
    Message msg;
    while (co_await msg_stream.receive(msg)) {
        if ((msg.content_type == repl_content_type)
            && (filter.empty() || msg.target == filter)) {
                auto &trn = shared_transaction(storage, CommitMode::lazy);
                replicate_from_message(msg.payload, trn );
        }
    }
}

}
