#include "../quarkbot/common/storage_msgbus_replicator.hpp"
#include "../quarkbot/common/mem_storage.hpp"
#include "quarkbot/storage.hpp"
#include "tests/check.h"

#include <string>
#include <vector>

using namespace quarkbot;
using Type = Storage::ReplicatorEvent::Type;

///Collected copy of a ReplicatorEvent - the event itself only borrows its buffers
struct CapturedEvent {
    Type type;
    std::string name;
    RecordKey recordkey;
    std::string value;
    srl::SchemaHash schema_hash;
};

static std::string encode(const Storage::ReplicatorEvent &ev) {
    std::vector<char> buffer(replication_message_size(ev));
    auto msg = encode_replication_message(ev, buffer);
    return std::string(msg.data(), msg.size());
}

///decode a message into a fresh MemStorage and return what that storage re-emitted
static std::vector<CapturedEvent> round_trip(const Storage::ReplicatorEvent &ev) {
    auto target = MemStorage::create();
    std::vector<CapturedEvent> out;
    auto conn = IStorage::Replicator::create_connection(
        [&out](const IStorage::ReplicatorEvent &e) noexcept {
            out.push_back(CapturedEvent{e.type, std::string(e.name), e.recordkey,
                                        std::string(e.value), e.schema_hash});
        });
    target->add_replicator(conn);

    Storage stor(target);
    auto trn = stor.write();
    CHECK(replicate_from_message(encode(ev), trn));
    trn.commit();
    return out;
}

void test_put_key_value_round_trip() {
    auto out = round_trip({.type = Type::put_key_value, .name = "alpha",
                           .recordkey = {1,2}, .value = "a1"});
    CHECK_EQUAL(out.size(), 1u);
    CHECK(out[0].type == Type::put_key_value);
    CHECK_EQUAL(out[0].name, "alpha");
    CHECK((out[0].recordkey == RecordKey{1,2}));
    CHECK_EQUAL(out[0].value, "a1");
}

void test_put_schema_round_trip() {
    auto out = round_trip({.type = Type::put_schema, .value = "schema-blob",
                           .schema_hash = srl::SchemaHash{0xDEADBEEF}});
    CHECK_EQUAL(out.size(), 1u);
    CHECK(out[0].type == Type::put_schema);
    CHECK_EQUAL(out[0].schema_hash, srl::SchemaHash{0xDEADBEEF});
    CHECK_EQUAL(out[0].value, "schema-blob");
}

void test_update_latest_round_trip() {
    auto out = round_trip({.type = Type::update_latest, .name = "alpha", .recordkey = {7,8}});
    CHECK_EQUAL(out.size(), 1u);
    CHECK(out[0].type == Type::update_latest);
    CHECK_EQUAL(out[0].name, "alpha");
    CHECK((out[0].recordkey == RecordKey{7,8}));
}

void test_erase_key_round_trip() {
    auto out = round_trip({.type = Type::erase_key, .name = "alpha", .recordkey = {3,4}});
    CHECK_EQUAL(out.size(), 1u);
    CHECK(out[0].type == Type::erase_key);
    CHECK_EQUAL(out[0].name, "alpha");
    CHECK((out[0].recordkey == RecordKey{3,4}));
}

void test_erase_latest_round_trip() {
    auto out = round_trip({.type = Type::erase_latest, .name = "alpha"});
    CHECK_EQUAL(out.size(), 1u);
    CHECK(out[0].type == Type::erase_latest);
    CHECK_EQUAL(out[0].name, "alpha");
}

void test_erase_name_round_trip() {
    auto out = round_trip({.type = Type::erase_name, .name = "alpha"});
    // a MemStorage with nothing stored under "alpha" reports only the pointer removal
    CHECK_GREATER_EQUAL(out.size(), 1u);
    CHECK_EQUAL(out[0].name, "alpha");
}

///true when the message was accepted; the transaction is committed either way
static bool decodes(std::string_view msg) {
    auto target = MemStorage::create();
    Storage stor(target);
    auto trn = stor.write();
    bool r = replicate_from_message(msg, trn);
    trn.commit();
    return r;
}

void test_malformed_messages_are_rejected() {
    CHECK(!decodes(""));
    CHECK(!decodes("Z"));                    // unknown type byte

    // a recordkey cut short must be refused, not built from whatever bytes remain
    auto trunc = encode({.type = Type::update_latest, .name = "alpha", .recordkey = {7,8}});
    CHECK(!decodes(std::string_view(trunc).substr(0, trunc.size() - 1)));

    // likewise a schema hash cut short
    auto sch = encode({.type = Type::put_schema, .value = "b",
                       .schema_hash = srl::SchemaHash{0xDEADBEEF}});
    CHECK(!decodes(std::string_view(sch).substr(0, 4)));
}

int main() {
    test_put_key_value_round_trip();
    test_put_schema_round_trip();
    test_update_latest_round_trip();
    test_erase_key_round_trip();
    test_erase_latest_round_trip();
    test_erase_name_round_trip();
    test_malformed_messages_are_rejected();
    return 0;
}
