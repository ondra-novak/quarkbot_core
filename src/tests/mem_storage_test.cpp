#include "quarkbot/storage.hpp"
#include "tests/check.h"
#include "../quarkbot/common/mem_storage.hpp"
#include "quarkbot/storage_srl.hpp"   // IWYU pragma: keep - template definitions
#include <quarkbot/storage_namespace.hpp>
#include <memory>
#include <vector>

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
    p = 50;
    for (auto z: storage.select_range("data", {50,0}, {0,0})) {
        int val;
        if (z >> val) {
            CHECK_EQUAL(val, p);
            --p;
        }
    }

    CHECK_EQUAL(p, 0);

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


///forwards every committed change of `from` into `to`, one transaction per event
static IStorage::Replicator::Connection forward(const PStorage &from, const PStorage &to) {
    auto conn = IStorage::Replicator::create_connection(
        [to](IStorage::ReplicatorEvent ev) noexcept {
            auto tx = to->write();
            tx->put(ev);
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

void test_erase_replicates_as_erase() {
    auto storage = MemStorage::create();
    std::vector<bool> erase_flags;
    auto conn = IStorage::Replicator::create_connection(
        [&](IStorage::ReplicatorEvent ev) noexcept {
            erase_flags.push_back(ev.erase);
        });
    storage->add_replicator(conn);

    auto tx = storage->write();
    tx->put("var", {1,0}, "v1");
    tx->commit();
    erase_flags.clear();

    tx = storage->write();
    tx->erase("var");
    tx->commit();

    // the data record and the last-revision pointer, both as deletions
    CHECK_EQUAL(erase_flags.size(), 2u);
    for (bool f: erase_flags) CHECK(f);
}


int main() {
    test_put_get();
    test_erase();
    test_plain_get_all_keys();
    test_sequences();
    test_namespaces();
    test_replication_cascade();
    test_erase_replicates_as_erase();
    return 0;
}

