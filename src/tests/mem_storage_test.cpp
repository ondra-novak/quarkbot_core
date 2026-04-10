#include "impl/mem_storage.hpp"
#include "tests/check.h"
#include <memory>

using namespace quarkbot;

void test_plain_put_get() {
    MemStorage storage;

    // key not present → exists=false, rev=0
    auto v = storage.get({"foo", false});
    CHECK(!v.exists);
    CHECK_EQUAL(v.rev, 0u);

    // write via transaction
    auto tx = storage.write();
    auto r = tx->put({"foo", false}, "hello");
    CHECK_EQUAL(r, 0u);  // non-sequence always returns 0
    tx->commit();

    // now exists
    auto v2 = storage.get({"foo", false});
    CHECK(v2.exists);
    CHECK_EQUAL(v2.rev, 0u);
    CHECK_EQUAL(v2.data, "hello");

    // overwrite
    auto tx2 = storage.write();
    tx2->put({"foo", false}, "world");
    tx2->commit();

    auto v3 = storage.get({"foo", false});
    CHECK(v3.exists);
    CHECK_EQUAL(v3.data, "world");

    // different key still absent
    auto v4 = storage.get({"bar", false});
    CHECK(!v4.exists);

    // non-committed transaction has no effect
    auto tx3 = storage.write();
    tx3->put({"foo", false}, "dropped");
    // no commit — let tx3 go out of scope
    auto v5 = storage.get({"foo", false});
    CHECK_EQUAL(v5.data, "world");
}

void test_plain_erase() {
    MemStorage storage;
    auto tx = storage.write();
    tx->put({"key", false}, "value");
    tx->commit();

    CHECK(storage.get({"key", false}).exists);

    auto tx2 = storage.write();
    auto r = tx2->erase({"key", false});
    CHECK_EQUAL(r, 0u);
    tx2->commit();

    // after erase, key is gone — indistinguishable from never having existed
    CHECK(!storage.get({"key", false}).exists);
    CHECK_EQUAL(storage.get({"key", false}).rev, 0u);
}

void test_plain_get_all_keys() {
    MemStorage storage;
    auto tx = storage.write();
    tx->put({"order:1", false}, "a");
    tx->put({"order:2", false}, "b");
    tx->put({"fill:1", false}, "c");
    tx->commit();

    // prefix filter
    auto keys = storage.get_all_keys({"order:", false});
    CHECK_EQUAL(keys.size(), 2u);
    // both order keys present (order not guaranteed)
    bool has1 = false, has2 = false;
    for (auto &k : keys) {
        if (k == "order:1") has1 = true;
        if (k == "order:2") has2 = true;
    }
    CHECK(has1);
    CHECK(has2);

    // empty prefix = all plain keys
    auto all = storage.get_all_keys({"", false});
    CHECK_EQUAL(all.size(), 3u);

    // erased key not returned
    auto tx2 = storage.write();
    tx2->erase({"order:1", false});
    tx2->commit();

    auto keys2 = storage.get_all_keys({"order:", false});
    CHECK_EQUAL(keys2.size(), 1u);
    CHECK_EQUAL(keys2[0], "order:2");
}

void test_seq_put_get_latest() {
    MemStorage storage;

    // key absent
    auto v0 = storage.get({"events", true});
    CHECK(!v0.exists);
    CHECK_EQUAL(v0.rev, 0u);

    // first put → revision 1
    auto tx = storage.write();
    auto r1 = tx->put({"events", true}, "data1");
    CHECK_EQUAL(r1, 1u);
    tx->commit();

    auto v1 = storage.get({"events", true});
    CHECK(v1.exists);
    CHECK_EQUAL(v1.rev, 1u);
    CHECK_EQUAL(v1.data, "data1");

    // second put → revision 2
    auto tx2 = storage.write();
    auto r2 = tx2->put({"events", true}, "data2");
    CHECK_EQUAL(r2, 2u);
    tx2->commit();

    auto v2 = storage.get({"events", true});
    CHECK(v2.exists);
    CHECK_EQUAL(v2.rev, 2u);
    CHECK_EQUAL(v2.data, "data2");

    // third put → revision 3
    auto tx3 = storage.write();
    tx3->put({"events", true}, "data3");
    tx3->commit();

    auto v3 = storage.get({"events", true});
    CHECK(v3.exists);
    CHECK_EQUAL(v3.rev, 3u);
    CHECK_EQUAL(v3.data, "data3");

    // uncommitted transaction must not affect visible state
    auto tx4 = storage.write();
    tx4->put({"events", true}, "dropped");
    // no commit — tx4 goes out of scope
    auto v4 = storage.get({"events", true});
    CHECK_EQUAL(v4.rev, 3u);
    CHECK_EQUAL(v4.data, "data3");
}

void test_seq_get_by_revision() {
    MemStorage storage;

    auto tx = storage.write();
    tx->put({"log", true}, "entry1");
    tx->put({"log", true}, "entry2");  // two puts in one tx — both committed
    tx->commit();

    // After commit: rev 1 = "entry1", rev 2 = "entry2"
    auto v1 = storage.get({"log", true}, 1);
    CHECK(v1.exists);
    CHECK_EQUAL(v1.rev, 1u);
    CHECK_EQUAL(v1.data, "entry1");

    auto v2 = storage.get({"log", true}, 2);
    CHECK(v2.exists);
    CHECK_EQUAL(v2.rev, 2u);
    CHECK_EQUAL(v2.data, "entry2");

    // latest is rev 2
    auto vl = storage.get({"log", true});
    CHECK_EQUAL(vl.rev, 2u);

    // non-existent revision
    auto vx = storage.get({"log", true}, 99);
    CHECK(!vx.exists);
    CHECK_EQUAL(vx.rev, 0u);
}

void test_seq_erase_tombstone() {
    MemStorage storage;

    auto tx = storage.write();
    tx->put({"items", true}, "first");
    tx->commit();

    CHECK_EQUAL(storage.get({"items", true}).rev, 1u);
    CHECK(storage.get({"items", true}).exists);

    // erase → tombstone with next revision
    auto tx2 = storage.write();
    auto r = tx2->erase({"items", true});
    CHECK_EQUAL(r, 2u);
    tx2->commit();

    // latest value: exists=false, rev=2 (tombstone)
    auto v = storage.get({"items", true});
    CHECK(!v.exists);
    CHECK_EQUAL(v.rev, 2u);

    // old revision still accessible and valid
    auto v1 = storage.get({"items", true}, 1);
    CHECK(v1.exists);
    CHECK_EQUAL(v1.data, "first");

    // tombstone accessible by revision
    auto v2 = storage.get({"items", true}, 2);
    CHECK(!v2.exists);
    CHECK_EQUAL(v2.rev, 2u);

    // further put after tombstone → revision 3
    auto tx3 = storage.write();
    tx3->put({"items", true}, "restored");
    tx3->commit();

    auto v3 = storage.get({"items", true});
    CHECK(v3.exists);
    CHECK_EQUAL(v3.rev, 3u);
    CHECK_EQUAL(v3.data, "restored");
}

int main() {
    test_plain_put_get();
    test_plain_erase();
    test_plain_get_all_keys();
    test_seq_put_get_latest();
    test_seq_get_by_revision();
    test_seq_erase_tombstone();
    return 0;
}
