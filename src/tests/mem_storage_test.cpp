#include "quarkbot/storage.hpp"
#include "tests/check.h"
#include "../quarkbot/common/mem_storage.hpp"
#include "quarkbot/storage_srl.hpp"   // IWYU pragma: keep - template definitions
#include <quarkbot/storage_namespace.hpp>
#include <memory>
#include <vector>
#include <string>
#include <limits>

using namespace quarkbot;

void test_put_get() {
    auto storage = quarkbot::MemStorage::create();

    // key not present → exists=false, rev=0
    auto v = storage->get("foo");
    CHECK(!v.exists);    

    // write via transaction
    auto tx = storage->write();
    auto r = tx->put("foo",  "hello");
    CHECK(r.ordered > 0);
    tx->commit();

    // now exists
    auto v2 = storage->get("foo");
    CHECK(v2.exists);
    CHECK(v2.key== r);
    CHECK_EQUAL(v2.data, "hello");

    // overwrite
    auto tx2 = storage->write();
    auto r3 = tx2->put("foo", "world");
    tx2->commit();

    auto v3 = storage->get("foo");
    CHECK(v3.exists);
    CHECK_EQUAL(v3.data, "world");
    CHECK(v3.key== r3);

    // different key still absent
    auto v4 = storage->get("bar");
    CHECK(!v4.exists);

    // non-committed transaction has no effect
    auto tx3 = storage->write();
    tx3->put("foo", "dropped");
    // no commit — let tx3 go out of scope
    tx3.reset();
    auto v5 = storage->get("foo");
    CHECK_EQUAL(v5.data, "world");
}

void test_erase() {
    auto storage = quarkbot::MemStorage::create();
    auto tx = storage->write();
    auto r1 = tx->put("key", "value");
    tx->commit();

    tx = storage->write();
    auto r2 = tx->put("key", "value");
    tx->commit();

    CHECK(storage->get("key").exists);

    tx = storage->write();
    tx->erase("key");    
    tx->commit();

    // after erase, key is gone — indistinguishable from never having existed
    CHECK(!storage->get("key").exists);
    CHECK(!storage->get("key",r1).exists);
    CHECK(!storage->get("key",r2).exists);
  }

void test_plain_get_all_keys() {
    auto storage = quarkbot::MemStorage::create();
    auto tx = storage->write();
    tx->put("order:1", "a");
    tx->put("order:2", "b");
    tx->put("fill:1", "c");
    tx->commit();

    tx = storage->write();
    tx->put("order:1", "d");
    tx->commit();

    // prefix filter
    auto keys = storage->list("order");
    CHECK_EQUAL(keys.size(), 2u);
    // both order keys present (order not guaranteed)
    bool has1 = false, has2 = false;
    for (auto &k : keys) {
        if (k == "order:1") {
            CHECK(!has1);
            has1 = true;
        }
        if (k == "order:2"){
            CHECK(!has2);
            has2 = true;
        }
    }
    CHECK(has1);
    CHECK(has2);

}

void test_sequences() {
    Storage storage = MemStorage::create();

    for (int i = 0; i < 100; ++i) {
        auto wr = storage.write();
        wr.store("data", {static_cast<std::uint64_t>(i),0}, i);
        wr.commit();
    }

    int p = 10;
    for (auto z: storage.select_range("data", {10,0}, {20,0})) {
        int val;
        if (z >> val) {
   
            CHECK_EQUAL(val, p);
            ++p;
        }
    }
    CHECK_EQUAL(p, 20);
    p = 49;
    for (auto z: storage.select_range("data", {50,0}, {0,0}, RangeDirection::descending)) {
        int val;
        if (z >> val) {
            CHECK_EQUAL(val, p);
            --p;
        }
    }

    CHECK_EQUAL(p, -1);

}

void test_namespaces() {
    auto storage = quarkbot::MemStorage::create();
    auto ns1 = storage->create_namespace(storage, "ns1/");
    auto ns2 = storage->create_namespace(storage, "ns2/");
    
    auto tx = ns1->write();
    tx->put("order", "a");
    tx->put("fill", "b");
    tx->commit();
    tx = ns2->write();
    tx->put("order", "c");
    tx->put("fill", "d");
    tx->commit();

    CHECK_EQUAL(ns1->get("order").data , "a");
    CHECK_EQUAL(ns1->get("fill").data , "b");
    CHECK_EQUAL(ns2->get("order").data , "c");
    CHECK_EQUAL(ns2->get("fill").data , "d");

    auto lst = ns1->list();
    CHECK_EQUAL(lst.size(), 2);
    CHECK_EQUAL(lst[0] ,"fill");
    CHECK_EQUAL(lst[1] ,"order");

    lst = ns2->list();
    CHECK_EQUAL(lst.size(), 2);
    CHECK_EQUAL(lst[0] ,"fill");
    CHECK_EQUAL(lst[1] ,"order");

    lst = storage->list();

    CHECK_EQUAL(lst.size(), 4);
    CHECK_EQUAL(lst[0] ,"ns1/fill");
    CHECK_EQUAL(lst[1] ,"ns1/order");
    CHECK_EQUAL(lst[2] ,"ns2/fill");
    CHECK_EQUAL(lst[3] ,"ns2/order");
}


static constexpr auto asc = RangeDirection::ascending;
static constexpr auto desc = RangeDirection::descending;

static_assert(RecordKey::first(5) == RecordKey{5,0});
static_assert(RecordKey::after(5) == RecordKey{6,0});
static_assert(RecordKey::after(std::numeric_limits<std::uint64_t>::max()) == RecordKey::max(),
        "after() must saturate instead of wrapping the range inside out");

static void check_range(const Storage &storage, const RecordKey &from, const RecordKey &to,
        RangeDirection dir, const std::vector<std::string> &expected) {
    std::vector<std::string> got;
    for (auto v: storage.select_range("data", from, to, dir)) got.emplace_back(v.data);
    CHECK_EQUAL(got.size(), expected.size());
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        CHECK_EQUAL(got[i], expected[i]);
    }
}

///The bounds always delimit [lower, upper) - lower included, upper excluded - so a
///range selects the same records whichever way it is traversed. MemStorage must
///agree with the other backend on every case here.
void test_range_direction() {
    Storage storage = MemStorage::create();
    auto tx = storage.write();
    for (std::uint64_t i = 10; i <= 50; i += 10) tx.put("data", {i,0}, "v" + std::to_string(i));
    tx.commit();

    // lower bound included, upper excluded - in both directions
    check_range(storage, {10,0}, {50,0}, asc,  {"v10","v20","v30","v40"});
    check_range(storage, {50,0}, {10,0}, desc, {"v40","v30","v20","v10"});

    // bounds without an exact record land on the same interval
    check_range(storage, {15,0}, {45,0}, asc,  {"v20","v30","v40"});
    check_range(storage, {45,0}, {15,0}, desc, {"v40","v30","v20"});

    // the idiom for "ordered value in <a,b>", written once and read either way
    check_range(storage, RecordKey::first(10), RecordKey::after(50), asc,
            {"v10","v20","v30","v40","v50"});
    check_range(storage, RecordKey::after(50), RecordKey::first(10), desc,
            {"v50","v40","v30","v20","v10"});

    // whole variable
    check_range(storage, RecordKey::min(), RecordKey::max(), asc,
            {"v10","v20","v30","v40","v50"});
    check_range(storage, RecordKey::max(), RecordKey::min(), desc,
            {"v50","v40","v30","v20","v10"});

    // a direction contradicting the bounds is a mistake, not a reversal
    check_range(storage, {10,0}, {50,0}, desc, {});
    check_range(storage, {50,0}, {10,0}, asc,  {});

    // equal bounds are empty whichever direction is declared
    check_range(storage, {30,0}, {30,0}, asc,  {});
    check_range(storage, {30,0}, {30,0}, desc, {});

    // ranges outside the data
    check_range(storage, {60,0}, {99,0}, asc,  {});
    check_range(storage, {99,0}, {60,0}, desc, {});
    check_range(storage, {0,0},  {5,0},  asc,  {});
    check_range(storage, {5,0},  {0,0},  desc, {});
}

///Only `ordered` is meaningfully ordered - `random` merely disambiguates records
///sharing it. A range therefore has to be expressed at `ordered` granularity, which
///is what RecordKey::after() is for.
void test_records_sharing_ordered_value() {
    Storage storage = MemStorage::create();
    auto tx = storage.write();
    tx.put("data", {30, 0}, "a");
    tx.put("data", {30, 7}, "b");
    tx.put("data", {40, 0}, "c");
    tx.commit();

    // after(30) reaches past every record at 30, so all of them are included
    check_range(storage, RecordKey::first(30), RecordKey::after(30), asc,  {"a","b"});
    check_range(storage, RecordKey::after(30), RecordKey::first(30), desc, {"b","a"});

    // stopping at {30,0} would silently drop the sibling records
    check_range(storage, RecordKey::first(30), RecordKey::first(31), asc, {"a","b"});
    check_range(storage, {30,0}, {40,0}, desc, {});
}

///Collected copy of a ReplicatorEvent - the event itself only borrows its buffers
struct CapturedEvent {
    IStorage::ReplicatorEvent::Type type;
    std::string name;
    RecordKey recordkey;
    std::string value;
    srl::SchemaHash schema_hash;
};

///Records every event a storage emits, in order
class EventLog {
public:
    IStorage::Replicator::Connection attach(const PStorage &storage) {
        auto conn = IStorage::Replicator::create_connection(
            [this](const IStorage::ReplicatorEvent &ev) noexcept {
                events.push_back(CapturedEvent{ev.type, std::string(ev.name), ev.recordkey,
                                               std::string(ev.value), ev.schema_hash});
            });
        storage->add_replicator(conn);
        return conn;
    }
    void clear() {events.clear();}
    std::vector<CapturedEvent> events;
};

void test_put_event_sequence() {
    using Type = IStorage::ReplicatorEvent::Type;
    auto storage = MemStorage::create();
    EventLog log;
    auto conn = log.attach(storage);

    // disable: the data record alone, no pointer
    auto tx = storage->write();
    tx->put("v", {1,0}, "c1", UpdateLastRevision::disable);
    tx->commit();
    CHECK_EQUAL(log.events.size(), 1u);
    CHECK(log.events[0].type == Type::put_key_value);
    CHECK_EQUAL(log.events[0].name, "v");
    CHECK((log.events[0].recordkey == RecordKey{1,0}));
    CHECK_EQUAL(log.events[0].value, "c1");

    // enable: the data record, then the pointer
    log.clear();
    tx = storage->write();
    tx->put("v", {2,0}, "c2", UpdateLastRevision::enable);
    tx->commit();
    CHECK_EQUAL(log.events.size(), 2u);
    CHECK(log.events[0].type == Type::put_key_value);
    CHECK((log.events[0].recordkey == RecordKey{2,0}));
    CHECK(log.events[1].type == Type::update_latest);
    CHECK_EQUAL(log.events[1].name, "v");
    CHECK((log.events[1].recordkey == RecordKey{2,0}));

    // enable_erase_last: the data record, the erase of the *previous* revision, the pointer
    log.clear();
    tx = storage->write();
    tx->put("v", {3,0}, "c3", UpdateLastRevision::enable_erase_last);
    tx->commit();
    CHECK_EQUAL(log.events.size(), 3u);
    CHECK(log.events[0].type == Type::put_key_value);
    CHECK((log.events[0].recordkey == RecordKey{3,0}));
    CHECK(log.events[1].type == Type::erase_key);
    CHECK_EQUAL(log.events[1].name, "v");
    CHECK((log.events[1].recordkey == RecordKey{2,0}));
    CHECK(log.events[2].type == Type::update_latest);
    CHECK((log.events[2].recordkey == RecordKey{3,0}));
}

void test_schema_event_is_numeric() {
    using Type = IStorage::ReplicatorEvent::Type;
    auto storage = MemStorage::create();
    EventLog log;
    auto conn = log.attach(storage);

    auto tx = storage->write();
    tx->put_schema_binary(srl::SchemaHash{0x1234}, "blob");
    tx->commit();

    CHECK_EQUAL(log.events.size(), 1u);
    CHECK(log.events[0].type == Type::put_schema);
    CHECK_EQUAL(log.events[0].schema_hash, srl::SchemaHash{0x1234});
    CHECK_EQUAL(log.events[0].value, "blob");
}

void test_erase_single_revision_event() {
    using Type = IStorage::ReplicatorEvent::Type;
    auto storage = MemStorage::create();
    auto tx = storage->write();
    tx->put("v", {1,0}, "c1", UpdateLastRevision::disable);
    tx->commit();

    EventLog log;
    auto conn = log.attach(storage);
    tx = storage->write();
    tx->erase("v", {1,0});
    tx->commit();

    CHECK_EQUAL(log.events.size(), 1u);
    CHECK(log.events[0].type == Type::erase_key);
    CHECK_EQUAL(log.events[0].name, "v");
    CHECK((log.events[0].recordkey == RecordKey{1,0}));
}

void test_apply_into_namespace() {
    using Type = IStorage::ReplicatorEvent::Type;
    auto root = MemStorage::create();
    auto ns = IStorage::create_namespace(root, "ns/");

    auto tx = ns->write();
    tx->apply(IStorage::ReplicatorEvent{.type = Type::put_key_value, .name = "alpha",
                                        .recordkey = {1,0}, .value = "a1"});
    tx->apply(IStorage::ReplicatorEvent{.type = Type::update_latest, .name = "alpha",
                                        .recordkey = {1,0}});
    tx->commit();

    // visible inside the namespace and under the prefixed name, never at the root name
    CHECK_EQUAL(ns->get("alpha", RecordKey{1,0}).data, "a1");
    CHECK_EQUAL(ns->get("alpha").data, "a1");
    CHECK_EQUAL(root->get("ns/alpha", RecordKey{1,0}).data, "a1");
    CHECK(!root->get("alpha", RecordKey{1,0}).exists);

    // a same-named variable outside the namespace must survive erase_name inside it
    tx = root->write();
    tx->put("alpha", {9,0}, "root-value");
    tx->commit();

    tx = ns->write();
    tx->apply(IStorage::ReplicatorEvent{.type = Type::erase_name, .name = "alpha"});
    tx->commit();

    CHECK(!ns->get("alpha", RecordKey{1,0}).exists);
    CHECK_EQUAL(root->get("alpha", RecordKey{9,0}).data, "root-value");
}

///forwards every committed change of `from` into `to`, one transaction per event
static IStorage::Replicator::Connection forward(const PStorage &from, const PStorage &to) {
    auto conn = IStorage::Replicator::create_connection(
        [to](const IStorage::ReplicatorEvent &ev) noexcept {
            auto tx = to->write();
            tx->apply(ev);
            tx->commit();
        });
    from->add_replicator(conn);
    return conn;
}

void test_replication_cascade() {
    auto a = MemStorage::create();
    auto b = MemStorage::create();
    auto c = MemStorage::create();
    auto ab = forward(a, b);
    auto bc = forward(b, c);

    auto tx = a->write();
    tx->put("var", {1,0}, "v1");
    tx->put("var", {2,0}, "v2");
    tx->put_schema_binary(srl::SchemaHash{0x1234}, "schema-blob");
    tx->commit();

    // b is a direct replica, c is a replica of a replica
    for (const auto &s: {b, c}) {
        CHECK_EQUAL(s->get("var").data, "v2");
        CHECK_EQUAL(s->get("var", RecordKey{1,0}).data, "v1");
        CHECK_EQUAL(s->get_schema_binary(srl::SchemaHash{0x1234}).data, "schema-blob");
        CHECK_EQUAL(s->list().size(), 1u);
    }

    tx = a->write();
    tx->erase("var");
    tx->commit();

    for (const auto &s: {b, c}) {
        CHECK(!s->get("var").exists);
        CHECK(!s->get("var", RecordKey{1,0}).exists);
        CHECK(!s->get("var", RecordKey{2,0}).exists);
    }
}

void test_erase_emits_erase_name() {
    using Type = IStorage::ReplicatorEvent::Type;
    auto storage = MemStorage::create();
    auto tx = storage->write();
    tx->put("var", {1,0}, "v1");
    tx->put("var", {2,0}, "v2");
    tx->commit();

    EventLog log;
    auto conn = log.attach(storage);
    tx = storage->write();
    tx->erase("var");
    tx->commit();

    // MemStorage has the bulk intent, so it states it once rather than record by record
    CHECK_EQUAL(log.events.size(), 1u);
    CHECK(log.events[0].type == Type::erase_name);
    CHECK_EQUAL(log.events[0].name, "var");
}


int main() {
    test_put_get();
    test_erase();
    test_plain_get_all_keys();
    test_sequences();
    test_namespaces();
    test_range_direction();
    test_records_sharing_ordered_value();
    test_replication_cascade();
    test_erase_emits_erase_name();
    test_put_event_sequence();
    test_schema_event_is_numeric();
    test_erase_single_revision_event();
    test_apply_into_namespace();
    return 0;
}

