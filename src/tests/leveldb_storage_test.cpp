#include "../quarkbot/leveldb/leveldb_storage.hpp"
#include "../quarkbot/common/mem_storage.hpp"
#include "quarkbot/storage.hpp"
#include "tests/check.h"

#include <algorithm>
#include <filesystem>
#include <leveldb/options.h>
#include <leveldb/status.h>
#include <string>
#include <vector>

using namespace quarkbot;

static std::filesystem::path db_path(const char *name) {
    return std::filesystem::temp_directory_path() / (std::string("quarkbot_leveldb_test_") + name);
}

static LevelDBStorageManager open_fresh(const std::filesystem::path &path) {
    std::filesystem::remove_all(path);
    leveldb::Options opts;
    opts.create_if_missing = true;
    return LevelDBStorageManager::open_db(path, opts);
}

static LevelDBStorageManager open_existing(const std::filesystem::path &path) {
    leveldb::Options opts;
    opts.create_if_missing = false;
    return LevelDBStorageManager::open_db(path, opts);
}

static int keyspace_of(const PStorage &s) {
    auto p = std::dynamic_pointer_cast<LevelDBStorage>(s);
    if (!p) {
        std::cerr << "FAILED: not a LevelDBStorage" << std::endl;
        exit(1);
    }
    return p->get_keyspace_id();
}

static void check_ok(const leveldb::Status &st) {
    CHECK(st.ok());
}

///RAII guard removing the database directory on scope exit
struct TempDB {
    std::filesystem::path path;
    explicit TempDB(const char *name):path(db_path(name)) {}
    ~TempDB() {std::filesystem::remove_all(path);}
};


void test_manager_directory() {
    TempDB tmp("manager");
    auto mgr = open_fresh(tmp.path);

    auto alpha = mgr.get_storage("alpha");
    auto beta = mgr.get_storage("beta");

    // distinct storages get distinct keyspaces
    CHECK_NOT_EQUAL(keyspace_of(alpha), keyspace_of(beta));
    // the same name always resolves to the same keyspace
    CHECK_EQUAL(keyspace_of(mgr.get_storage("alpha")), keyspace_of(alpha));

    auto lst = mgr.list();
    std::sort(lst.begin(), lst.end());
    CHECK_EQUAL(lst.size(), 2u);
    CHECK_EQUAL(lst[0], "alpha");
    CHECK_EQUAL(lst[1], "beta");
}

void test_delete_storage() {
    TempDB tmp("delete_storage");
    auto mgr = open_fresh(tmp.path);

    auto alpha = mgr.get_storage("alpha");
    auto tx = alpha->write();
    tx->put("var", "content");
    tx->commit();
    CHECK(alpha->get("var").exists);

    mgr.delete_storage("alpha");
    CHECK_EQUAL(mgr.list().size(), 0u);

    // recreated on demand and empty
    auto alpha2 = mgr.get_storage("alpha");
    CHECK(!alpha2->get("var").exists);
    CHECK_EQUAL(alpha2->list().size(), 0u);
}

///a directory record whose value is empty must not resolve to keyspace 0 and
///silently alias an unrelated storage
void test_truncated_directory_entry() {
    TempDB tmp("corrupt_dir");
    auto mgr = open_fresh(tmp.path);

    auto good = mgr.get_storage("good");
    auto tx = good->write();
    tx->put("var", "belongs_to_good");
    tx->commit();

    // forge a truncated directory record, as a partial write could leave behind
    std::string key;
    key.push_back(static_cast<char>(LevelDBStorageManager::directory_id));
    key.append("broken");
    check_ok(mgr.get_db()->Put({}, key, ""));

    auto broken = mgr.get_storage("broken");
    CHECK_NOT_EQUAL(keyspace_of(broken), keyspace_of(good));
    CHECK(!broken->get("var").exists);

    tx = broken->write();
    tx->put("var", "belongs_to_broken");
    tx->commit();
    CHECK_EQUAL(good->get("var").data, "belongs_to_good");
    CHECK_EQUAL(broken->get("var").data, "belongs_to_broken");
}

void test_put_get() {
    TempDB tmp("put_get");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    CHECK(!storage->get("foo").exists);

    auto tx = storage->write();
    auto r1 = tx->put("foo", "hello");
    // nothing visible before commit
    CHECK(!storage->get("foo").exists);
    tx->commit();

    auto v = storage->get("foo");
    CHECK(v.exists);
    CHECK(v.key == r1);
    CHECK_EQUAL(v.data, "hello");
    CHECK_EQUAL(storage->get("foo", r1).data, "hello");

    // second revision becomes the latest, first one is still reachable by key
    auto tx2 = storage->write();
    auto r2 = tx2->put("foo", "world");
    tx2->commit();

    CHECK_EQUAL(storage->get("foo").data, "world");
    CHECK(storage->get("foo").key == r2);
    CHECK_EQUAL(storage->get("foo", r1).data, "hello");

    CHECK(!storage->get("bar").exists);

    // dropped transaction has no effect
    auto tx3 = storage->write();
    tx3->put("foo", "dropped");
    tx3.reset();
    CHECK_EQUAL(storage->get("foo").data, "world");
}

void test_keyspace_isolation() {
    TempDB tmp("isolation");
    auto mgr = open_fresh(tmp.path);
    auto a = mgr.get_storage("a");
    auto b = mgr.get_storage("b");

    auto tx = a->write();
    tx->put("shared_name", "from_a");
    tx->commit();
    tx = b->write();
    tx->put("shared_name", "from_b");
    tx->commit();

    CHECK_EQUAL(a->get("shared_name").data, "from_a");
    CHECK_EQUAL(b->get("shared_name").data, "from_b");
    CHECK_EQUAL(a->list().size(), 1u);
    CHECK_EQUAL(b->list().size(), 1u);
}

void test_erase_variable() {
    TempDB tmp("erase_variable");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    auto tx = storage->write();
    auto r1 = tx->put("key", "v1");
    tx->commit();
    tx = storage->write();
    auto r2 = tx->put("key", "v2");
    tx->put("other", "keep");
    tx->commit();

    CHECK(storage->get("key").exists);

    tx = storage->write();
    tx->erase("key");
    tx->commit();

    // every revision and the last-revision pointer are gone
    CHECK(!storage->get("key").exists);
    CHECK(!storage->get("key", r1).exists);
    CHECK(!storage->get("key", r2).exists);
    // neighbouring variable untouched
    CHECK_EQUAL(storage->get("other").data, "keep");
    auto lst = storage->list();
    CHECK_EQUAL(lst.size(), 1u);
    CHECK_EQUAL(lst[0], "other");
}

void test_erase_single_revision() {
    TempDB tmp("erase_revision");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    auto tx = storage->write();
    tx->put("key", {1,0}, "v1");
    tx->put("key", {2,0}, "v2");
    tx->commit();

    tx = storage->write();
    tx->erase("key", {1,0});
    tx->commit();

    CHECK(!storage->get("key", RecordKey{1,0}).exists);
    CHECK_EQUAL(storage->get("key", RecordKey{2,0}).data, "v2");
    CHECK_EQUAL(storage->get("key").data, "v2");
}

void test_list_prefix() {
    TempDB tmp("list_prefix");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    auto tx = storage->write();
    tx->put("order:1", "a");
    tx->put("order:2", "b");
    tx->put("fill:1", "c");
    tx->put_schema_binary(srl::SchemaHash{}, "schema-blob");
    tx->commit();

    auto all = storage->list();
    std::sort(all.begin(), all.end());
    CHECK_EQUAL(all.size(), 3u);
    CHECK_EQUAL(all[0], "fill:1");
    CHECK_EQUAL(all[1], "order:1");
    CHECK_EQUAL(all[2], "order:2");

    auto orders = storage->list("order");
    std::sort(orders.begin(), orders.end());
    CHECK_EQUAL(orders.size(), 2u);
    CHECK_EQUAL(orders[0], "order:1");
    CHECK_EQUAL(orders[1], "order:2");
}

void test_select_range() {
    TempDB tmp("select_range");
    auto mgr = open_fresh(tmp.path);
    Storage storage = mgr.get_storage("s");

    for (std::uint64_t i = 0; i < 100; ++i) {
        auto wr = storage.write();
        wr.put("data", {i, 0}, "v" + std::to_string(i));
        wr.commit();
    }

    std::uint64_t p = 10;
    for (auto v: storage.select_range("data", {10,0}, {20,0})) {
        CHECK_EQUAL(v.key.ordered, p);
        CHECK_EQUAL(v.data, "v" + std::to_string(p));
        ++p;
    }
    CHECK_EQUAL(p, 20u);

    // reversed range iterates backwards, since inclusive, until exclusive
    p = 50;
    for (auto v: storage.select_range("data", {50,0}, {0,0})) {
        CHECK_EQUAL(v.key.ordered, p);
        CHECK_EQUAL(v.data, "v" + std::to_string(p));
        --p;
    }
    CHECK_EQUAL(p, 0u);

    // empty range
    int count = 0;
    for (auto v: storage.select_range("data", {5,0}, {5,0})) {(void)v; ++count;}
    CHECK_EQUAL(count, 0);
}

static void check_range(const Storage &storage, const RecordKey &since, const RecordKey &until,
        const std::vector<std::string> &expected) {
    std::vector<std::string> got;
    for (auto v: storage.select_range("data", since, until)) got.emplace_back(v.data);
    CHECK_EQUAL(got.size(), expected.size());
    for (std::size_t i = 0; i < expected.size() && i < got.size(); ++i) {
        CHECK_EQUAL(got[i], expected[i]);
    }
}

///same expectations as mem_storage_test::test_reverse_range_edges - both backends
///must answer identically, otherwise a strategy behaves differently when persisted
void test_reverse_range_edges() {
    TempDB tmp("reverse_edges");
    auto mgr = open_fresh(tmp.path);
    Storage storage = mgr.get_storage("s");

    auto tx = storage.write();
    for (std::uint64_t i = 10; i <= 50; i += 10) tx.put("data", {i,0}, "v" + std::to_string(i));
    tx.commit();

    check_range(storage, {50,0}, {20,0}, {"v50","v40","v30"});
    check_range(storage, {45,0}, {15,0}, {"v40","v30","v20"});
    check_range(storage, {35,0}, {5,0}, {"v30","v20","v10"});
    check_range(storage, {50,0}, {0,0}, {"v50","v40","v30","v20","v10"});
    check_range(storage, {99,0}, {40,0}, {"v50"});
    check_range(storage, {5,0}, {0,0}, {});
    check_range(storage, {20,0}, {50,0}, {"v20","v30","v40"});
    check_range(storage, {15,0}, {45,0}, {"v20","v30","v40"});
}

void test_update_last_revision() {
    TempDB tmp("last_revision");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    auto tx = storage->write();
    tx->put("v", {1,0}, "first", UpdateLastRevision::enable);
    tx->commit();
    CHECK_EQUAL(storage->get("v").data, "first");

    // disable: record is stored but the last-revision pointer stays put
    tx = storage->write();
    tx->put("v", {2,0}, "second", UpdateLastRevision::disable);
    tx->commit();
    CHECK_EQUAL(storage->get("v").data, "first");
    CHECK_EQUAL(storage->get("v", RecordKey{2,0}).data, "second");

    // enable_erase_last: pointer moves and the previous revision is dropped
    tx = storage->write();
    tx->put("v", {3,0}, "third", UpdateLastRevision::enable_erase_last);
    tx->commit();
    CHECK_EQUAL(storage->get("v").data, "third");
    CHECK(!storage->get("v", RecordKey{1,0}).exists);
    CHECK_EQUAL(storage->get("v", RecordKey{2,0}).data, "second");
}

void test_schema_binary() {
    TempDB tmp("schema");
    auto mgr = open_fresh(tmp.path);
    auto a = mgr.get_storage("a");
    auto b = mgr.get_storage("b");

    srl::SchemaHash h1 = 0x1122334455667788ULL;
    srl::SchemaHash h2 = 0x99aabbccddeeff00ULL;

    CHECK(!a->get_schema_binary(h1).exists);

    auto tx = a->write();
    tx->put_schema_binary(h1, "schema-one");
    tx->put_schema_binary(h2, "schema-two");
    tx->commit();

    CHECK_EQUAL(a->get_schema_binary(h1).data, "schema-one");
    CHECK_EQUAL(a->get_schema_binary(h2).data, "schema-two");
    // the schema keyspace is shared by every storage in the database
    CHECK_EQUAL(b->get_schema_binary(h1).data, "schema-one");
    // schemas are not variables
    CHECK_EQUAL(a->list().size(), 0u);
}

void test_persistence() {
    TempDB tmp("persistence");
    RecordKey key;
    {
        auto mgr = open_fresh(tmp.path);
        auto storage = mgr.get_storage("s");
        auto tx = storage->write();
        key = tx->put("survivor", "payload");
        tx->put_schema_binary(srl::SchemaHash{7}, "schema-blob");
        tx->commit(true);
    }
    {
        auto mgr = open_existing(tmp.path);
        auto lst = mgr.list();
        CHECK_EQUAL(lst.size(), 1u);
        CHECK_EQUAL(lst[0], "s");
        auto storage = mgr.get_storage("s");
        CHECK_EQUAL(storage->get("survivor").data, "payload");
        CHECK(storage->get("survivor").key == key);
        CHECK_EQUAL(storage->get_schema_binary(srl::SchemaHash{7}).data, "schema-blob");
    }
}


///Collected copy of a ReplicatorEvent - the event itself only borrows its buffers
struct CapturedEvent {
    std::string key;
    std::string value;
    bool erase;
    IStorage::ReplicatorEvent::Kind kind;
};

class EventLog {
public:
    IStorage::Replicator::Connection attach(const PStorage &storage) {
        auto conn = IStorage::Replicator::create_connection(
            [this](IStorage::ReplicatorEvent ev) noexcept {
                events.push_back({std::string(ev.key), std::string(ev.value), ev.erase, ev.kind});
            });
        storage->add_replicator(conn);
        return conn;
    }
    ///apply everything collected so far to another storage, in order
    void replay_into(const PStorage &target) const {
        auto tx = target->write();
        for (const auto &ev: events) {
            tx->put(IStorage::ReplicatorEvent{ev.key, ev.value, ev.erase, ev.kind});
        }
        tx->commit();
    }
    std::vector<CapturedEvent> events;
};

void test_replicator_fires_on_commit() {
    TempDB tmp("replicator_commit");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    EventLog log;
    auto conn = log.attach(storage);

    // dropped transaction emits nothing
    auto tx = storage->write();
    tx->put("v", "dropped");
    tx.reset();
    CHECK_EQUAL(log.events.size(), 0u);

    // events appear only after commit
    tx = storage->write();
    tx->put("v", {1,0}, "content", UpdateLastRevision::enable);
    CHECK_EQUAL(log.events.size(), 0u);
    tx->commit();
    // one data record plus the last-revision pointer
    CHECK_EQUAL(log.events.size(), 2u);
    for (const auto &ev: log.events) CHECK(!ev.erase);

    log.events.clear();
    tx = storage->write();
    tx->erase("v", {1,0});
    tx->commit();
    CHECK_EQUAL(log.events.size(), 1u);
    CHECK(log.events[0].erase);
}


///writes a fixed mix of records: two revisions, a generated-key record, a schema and an erase
static void write_replication_fixture(const PStorage &storage) {
    auto tx = storage->write();
    tx->put("alpha", {1,0}, "a1");
    tx->put("alpha", {2,0}, "a2");
    tx->put("beta", {5,0}, "b1");
    tx->put_schema_binary(srl::SchemaHash{0xABCD}, "schema-blob");
    tx->commit();

    tx = storage->write();
    tx->erase("alpha", {1,0});
    tx->commit();
}

///every record written by write_replication_fixture must be readable from storage
static void check_replication_fixture(const PStorage &storage) {
    CHECK(!storage->get("alpha", RecordKey{1,0}).exists);
    CHECK_EQUAL(storage->get("alpha", RecordKey{2,0}).data, "a2");
    CHECK_EQUAL(storage->get("alpha").data, "a2");
    CHECK_EQUAL(storage->get("beta").data, "b1");
    CHECK_EQUAL(storage->get_schema_binary(srl::SchemaHash{0xABCD}).data, "schema-blob");
    auto lst = storage->list();
    std::sort(lst.begin(), lst.end());
    CHECK_EQUAL(lst.size(), 2u);
    CHECK_EQUAL(lst[0], "alpha");
    CHECK_EQUAL(lst[1], "beta");
}

void test_replicated_key_is_logical() {
    TempDB tmp("repl_logical");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    EventLog log;
    auto conn = log.attach(storage);

    auto tx = storage->write();
    tx->put("alpha", {1,0}, "a1");
    tx->put_schema_binary(srl::SchemaHash{0xABCD}, "schema-blob");
    tx->commit();

    // data record + last-revision pointer + schema
    CHECK_EQUAL(log.events.size(), 3u);

    int schemas = 0;
    for (const auto &ev: log.events) {
        if (ev.kind == IStorage::ReplicatorEvent::Kind::schema) {
            ++schemas;
            CHECK_EQUAL(ev.value, "schema-blob");
            CHECK_EQUAL(ev.key.size(), sizeof(srl::SchemaHash));
        } else {
            // logical key: "alpha", or "alpha" + '\0' + 16 byte RecordKey.
            // No keyspace byte, so the name starts at offset 0.
            CHECK(ev.key.compare(0, 5, "alpha") == 0);
            CHECK(ev.key.size() == 5 || ev.key.size() == 5 + 1 + 2*sizeof(std::uint64_t));
        }
    }
    CHECK_EQUAL(schemas, 1);
}

void test_replication_to_other_keyspace() {
    TempDB src_tmp("repl_src");
    TempDB dst_tmp("repl_dst");
    auto src_mgr = open_fresh(src_tmp.path);
    auto dst_mgr = open_fresh(dst_tmp.path);

    // burn a few keyspaces in the source so source and target ids differ
    src_mgr.get_storage("filler1");
    src_mgr.get_storage("filler2");
    auto source = src_mgr.get_storage("source");
    auto target = dst_mgr.get_storage("target");
    CHECK_NOT_EQUAL(keyspace_of(source), keyspace_of(target));

    EventLog log;
    auto conn = log.attach(source);
    write_replication_fixture(source);

    check_replication_fixture(source);
    log.replay_into(target);
    check_replication_fixture(target);
}

void test_replication_to_mem_storage() {
    TempDB tmp("repl_mem");
    auto mgr = open_fresh(tmp.path);
    auto source = mgr.get_storage("source");
    auto target = MemStorage::create();

    EventLog log;
    auto conn = log.attach(source);
    write_replication_fixture(source);

    log.replay_into(target);
    check_replication_fixture(target);
}

int main() {
    test_manager_directory();
    test_delete_storage();
    test_truncated_directory_entry();
    test_put_get();
    test_keyspace_isolation();
    test_erase_variable();
    test_erase_single_revision();
    test_list_prefix();
    test_select_range();
    test_reverse_range_edges();
    test_update_last_revision();
    test_schema_binary();
    test_persistence();
    test_replicator_fires_on_commit();
    test_replicated_key_is_logical();
    test_replication_to_other_keyspace();
    test_replication_to_mem_storage();
    return 0;
}
