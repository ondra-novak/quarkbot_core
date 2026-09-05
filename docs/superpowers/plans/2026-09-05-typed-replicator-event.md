# Typed ReplicatorEvent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the binary key of `IStorage::ReplicatorEvent` with the logical fields it encodes (`name`, `recordkey`, `schema_hash`) plus an event-type enum, so replicating into a non-key-value store needs no copy of a backend's private key format.

**Architecture:** The event becomes a flat aggregate whose `Type` enum says which fields carry meaning — no union, no variant. Each backend decodes its own physical key format when emitting and encodes it back when applying. The change is rolled in with three *transitional* fields (`key`, `erase`, `is_schema`) kept on the struct so that read-only consumers can be migrated in their own task while every commit stays green; the last two tasks remove them and take the one deliberate behaviour change (MemStorage emitting a single `erase_name`).

**Tech Stack:** C++23, CMake, LevelDB, the macro test harness in `src/tests/check.h`. No external test framework.

**Spec:** `docs/superpowers/specs/2026-09-05-typed-replicator-event-design.md`

## Global Constraints

- Every task ends with the whole tree building and `ctest` green. No commit may leave a target uncompilable.
- Build and test with the already-configured build directory: `cmake --build build -j$(nproc)` then `ctest --test-dir build`. It is configured with `CMAKE_CXX_COMPILER=/usr/bin/clang++` and `QUARKBOT_LEVELDB:BOOL=ON`; do not reconfigure. If a fresh configure is ever needed: `cmake -DCMAKE_CXX_COMPILER=g++-14 -DQUARKBOT_LEVELDB=ON ..`. `QUARKBOT_LEVELDB` is **not** implied by `QUARKBOT_TESTS`, and without it neither the LevelDB backend nor its test compiles.
- `CHECK_EQUAL(a,b)` streams both operands with `operator<<`. `RecordKey` and `ReplicatorEvent::Type` have no `operator<<` — compare them with `CHECK(a == b)`, never `CHECK_EQUAL`.
- Use only the macros from `src/tests/check.h`: `CHECK`, `CHECK_EQUAL`, `CHECK_NOT_EQUAL`, `CHECK_GREATER`, `CHECK_EXCEPTION`. A failing macro calls `exit(1)`, so a test binary stops at the first failure.
- Comments in English, matching the density and tone of the surrounding code. Explain *why*, not *what*.
- Public interfaces use the `shared_ptr` aliases (`PStorage`, `PStorageTransaction`).
- `ReplicatorEvent`'s `std::string_view` members borrow foreign buffers (a committed `leveldb::WriteBatch`, a LevelDB slice, a `std::map` node). They are valid only for the duration of the handler call. Any handler that retains an event must copy it.
- Baseline before starting: `./build/tests/test_mem_storage_test` and `./build/tests/test_leveldb_storage_test` both pass.

---

### Task 1: Typed fields on ReplicatorEvent, emitted by both backends

Adds the typed fields and the `Type` enum next to the existing binary ones, switches the `Replicator` signal to a const reference, and makes both backends fill the typed fields on every event. The transitional fields stay populated so consumers, `replicate_from_message` and the existing assertions keep working untouched.

**Files:**
- Modify: `include/quarkbot/abstract/istorage.hpp:38-53` (the struct and the `Replicator` alias)
- Modify: `src/quarkbot/common/mem_storage.hpp:208-256` (`MemStorage::apply` overloads)
- Modify: `src/quarkbot/leveldb/leveldb_storage.hpp:90-110` (`ReplicatorHandler`)
- Modify: `src/quarkbot/leveldb/leveldb_storage.cpp:352-369` (`ReplicatorHandler::Put/Delete/emit`)
- Test: `src/tests/mem_storage_test.cpp`
- Test: `src/tests/leveldb_storage_test.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `quarkbot::IStorage::ReplicatorEvent::Type` — `enum class : std::uint8_t { put_schema, put_key_value, update_latest, erase_key, erase_latest, erase_name }`
  - `quarkbot::IStorage::ReplicatorEvent` fields `Type type; std::string_view name; RecordKey recordkey; std::string_view value; srl::SchemaHash schema_hash;` plus the transitional `std::string_view key; bool erase; bool is_schema;`
  - `quarkbot::IStorage::Replicator` = `signals::SignalSlot<void(const ReplicatorEvent &)>`
  - Test helpers in both test files: `struct CapturedEvent { IStorage::ReplicatorEvent::Type type; std::string name; RecordKey recordkey; std::string value; srl::SchemaHash schema_hash; };` and `class EventLog` with `Connection attach(const PStorage &)`, `void clear()`, `std::vector<CapturedEvent> events`.

- [ ] **Step 1: Confirm the baseline is green**

```bash
cd /home/ondra/vscode/quarkbot_core
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: build succeeds, all tests pass. Do not proceed if anything fails.

- [ ] **Step 2: Add the event-capture helper to `src/tests/mem_storage_test.cpp`**

Insert immediately above the existing `forward()` helper (around line 262). `mem_storage_test.cpp` already includes `<vector>`, `<string>` and `quarkbot/storage.hpp`.

```cpp
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
```

- [ ] **Step 3: Write the failing emission tests in `src/tests/mem_storage_test.cpp`**

Add these three functions after `EventLog`, and add `test_put_event_sequence(); test_schema_event_is_numeric(); test_erase_single_revision_event();` to `main()` (before `return 0;`).

```cpp
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
    CHECK(log.events[0].recordkey == RecordKey{1,0});
    CHECK_EQUAL(log.events[0].value, "c1");

    // enable: the data record, then the pointer
    log.clear();
    tx = storage->write();
    tx->put("v", {2,0}, "c2", UpdateLastRevision::enable);
    tx->commit();
    CHECK_EQUAL(log.events.size(), 2u);
    CHECK(log.events[0].type == Type::put_key_value);
    CHECK(log.events[0].recordkey == RecordKey{2,0});
    CHECK(log.events[1].type == Type::update_latest);
    CHECK_EQUAL(log.events[1].name, "v");
    CHECK(log.events[1].recordkey == RecordKey{2,0});

    // enable_erase_last: the data record, the erase of the *previous* revision, the pointer
    log.clear();
    tx = storage->write();
    tx->put("v", {3,0}, "c3", UpdateLastRevision::enable_erase_last);
    tx->commit();
    CHECK_EQUAL(log.events.size(), 3u);
    CHECK(log.events[0].type == Type::put_key_value);
    CHECK(log.events[0].recordkey == RecordKey{3,0});
    CHECK(log.events[1].type == Type::erase_key);
    CHECK_EQUAL(log.events[1].name, "v");
    CHECK(log.events[1].recordkey == RecordKey{2,0});
    CHECK(log.events[2].type == Type::update_latest);
    CHECK(log.events[2].recordkey == RecordKey{3,0});
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
    CHECK(log.events[0].recordkey == RecordKey{1,0});
}
```

- [ ] **Step 4: Run the test to verify it fails**

```bash
cmake --build build --target test_mem_storage_test -j$(nproc)
```

Expected: FAIL to compile — `no member named 'type' in 'quarkbot::IStorage::ReplicatorEvent'`. A compile error is the red state here.

- [ ] **Step 5: Add the `Type` enum and the typed fields to `include/quarkbot/abstract/istorage.hpp`**

Replace the whole `struct ReplicatorEvent { ... };` body (lines 38-53, including the doc comment above it) with:

```cpp
        ///Describes single record change performed by a committed transaction
        /**
            The event is *logical*: it never carries a backend specific keyspace or
            instance prefix, nor the binary key layout a backend happens to use. A
            backend decodes its own key format when emitting events and encodes it
            back when applying them, so an event stays applicable to a different
            keyspace, a different backend, or a store that is not key-value at all -
            a relational table keyed by (name, recordkey).

            Which fields carry meaning is decided by `type`:

            | type          | name | recordkey | value | schema_hash |
            |---------------|------|-----------|-------|-------------|
            | put_schema    |      |           |  yes  |     yes     |
            | put_key_value | yes  |    yes    |  yes  |             |
            | update_latest | yes  |    yes    |       |             |
            | erase_key     | yes  |    yes    |       |             |
            | erase_latest  | yes  |           |       |             |
            | erase_name    | yes  |           |       |             |

            The string_view members borrow their buffers and are valid only for the
            duration of the handler call. A handler that retains an event copies it.
        */
        struct ReplicatorEvent {

            ///Kind of the change; selects which of the fields below carry meaning
            enum class Type: std::uint8_t {
                ///a schema was stored - schema_hash, value
                put_schema,
                ///a record was written - name, recordkey, value
                put_key_value,
                ///the last-revision pointer of a variable now points at recordkey - name, recordkey
                update_latest,
                ///a single record was removed - name, recordkey
                erase_key,
                ///the last-revision pointer of a variable was removed - name
                erase_latest,
                ///every record of a variable was removed, its pointer included - name
                erase_name,
            };

            Type type;
            ///name of the variable
            std::string_view name = {};
            ///which revision of the variable
            RecordKey recordkey = {};
            ///stored content
            std::string_view value = {};
            ///hash of the stored schema
            srl::SchemaHash schema_hash = {};

            ///@{
            ///Transitional binary form, kept only until every consumer reads the typed
            ///fields above. Removed by the "remove the transitional fields" step of the
            ///typed-ReplicatorEvent change. Do not read these in new code.
            std::string_view key = {};
            bool erase = false;
            bool is_schema = false;
            ///@}
        };
```

Then change the `Replicator` alias on the following line from
`using Replicator = signals::SignalSlot<void(ReplicatorEvent)>;` to:

```cpp
        using Replicator = signals::SignalSlot<void(const ReplicatorEvent &)>;
```

- [ ] **Step 6: Verify the header compiles and locate every construction site**

```bash
cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head -30
```

Expected: errors only at aggregate-initialisation sites, because the field order changed and `type` has no default. They are in `src/quarkbot/common/mem_storage.hpp`, `src/quarkbot/leveldb/leveldb_storage.cpp`, `src/quarkbot/common/storage_msgbus_replicator.cpp` and the two test files. Steps 7-10 fix them.

- [ ] **Step 7: Rewrite the `MemStorage::apply` overloads in `src/quarkbot/common/mem_storage.hpp`**

Replace the four `apply` overloads (lines 208-256, from `apply(OpErase&&)` through `apply(OpPutSchema&&)`, leaving `schema_hash_to_key` / `schema_key_to_hash` in place) with:

```cpp
inline void MemStorage::apply(MemStorageTransaction::OpErase &&x) {
    using Type = ReplicatorEvent::Type;
    _storage.erase(x.variable);
    _watcher(ReplicatorEvent{.type = Type::erase_latest, .name = x.variable,
                             .key = x.variable, .erase = true});
    std::string beg = x.variable + '\0';
    std::string end = x.variable + '\x01';
    auto beg_iter = _storage.lower_bound(beg);
    auto end_iter = _storage.lower_bound(end);
    // +1 skips the '\0' separator between variable_name and the big-endian RecordKey
    auto sz = x.variable.size() + 1;
    auto iter = beg_iter;
    while (iter != end_iter ) {
        _watcher(ReplicatorEvent{.type = Type::erase_key, .name = x.variable,
                                 .recordkey = string_to_record_key(std::string_view(iter->first).substr(sz)),
                                 .key = iter->first, .erase = true});
        iter = _storage.erase(iter);
    }
}
inline void MemStorage::apply(MemStorageTransaction::OpEraseRev &&x) {
    using Type = ReplicatorEvent::Type;
    std::string key = wholeKey(x.variable, x.rev);
    _storage.erase(key);
    _watcher(ReplicatorEvent{.type = Type::erase_key, .name = x.variable, .recordkey = x.rev,
                             .key = key, .erase = true});
}
inline void MemStorage::apply(MemStorageTransaction::OpPut &&x) {
    using Type = ReplicatorEvent::Type;
    auto tmp = record_key_to_string(x.key);
    //the previous revision, when enable_erase_last drops it - reported after the new
    //record so that both backends emit put, erase, pointer in the same order
    std::optional<RecordKey> erased_rev;
    std::string erased_key;
    if (x.mode != UpdateLastRevision::disable) {
        auto &currev = _storage[x.variable];
        //the width check is what makes decoding safe - a pointer of any other length is
        //not a RecordKey, the same guard MemStorage::get applies before using one
        if (x.mode == UpdateLastRevision::enable_erase_last
                && currev.size() == recordkey_string_size) {
            erased_rev = string_to_record_key(currev);
            erased_key = wholeKey(x.variable, currev);
            _storage.erase(erased_key);
        }
        currev = tmp;
    }
    std::string key = wholeKey(x.variable, tmp);
    auto &ref = _storage[key] = std::move(x.data);
    _watcher(ReplicatorEvent{.type = Type::put_key_value, .name = x.variable,
                             .recordkey = x.key, .value = ref, .key = key});
    if (erased_rev) {
        _watcher(ReplicatorEvent{.type = Type::erase_key, .name = x.variable,
                                 .recordkey = *erased_rev, .key = erased_key, .erase = true});
    }
    if (x.mode != UpdateLastRevision::disable) {
        _watcher(ReplicatorEvent{.type = Type::update_latest, .name = x.variable,
                                 .recordkey = x.key, .value = tmp, .key = x.variable});
    }
}
```

Then move `schema_hash_to_key` / `schema_key_to_hash` (they are already defined just above `apply(OpPutSchema&&)`) and replace `apply(OpPutSchema&&)` with:

```cpp
inline void MemStorage::apply(MemStorageTransaction::OpPutSchema &&x) {
    using Type = ReplicatorEvent::Type;
    auto &ref = _schemas[x.hash] = std::move(x.schema);
    auto key = schema_hash_to_key(x.hash);
    _watcher(ReplicatorEvent{.type = Type::put_schema, .value = ref, .schema_hash = x.hash,
                             .key = {key.data(), key.size()}, .is_schema = true});
}
```

- [ ] **Step 8: Convert `MemStorage::apply(OpReplicate&&)` to designated initialisers**

`OpReplicate` still carries the binary form in this task; only the four `ReplicatorEvent{...}` constructions inside it need updating so they compile. Replace the body of `apply(MemStorageTransaction::OpReplicate &&x)` with:

```cpp
inline void MemStorage::apply(MemStorageTransaction::OpReplicate &&x) {
    using Type = ReplicatorEvent::Type;
    //the event is re-emitted from the stored copies, never from moved-from members,
    //so that cascaded replication (A -> B -> C) forwards the real content.
    //Transitional: still driven by the binary key. The typed-field rewrite lands with
    //IStorageTransaction::apply().
    if (x.is_schema) {
        auto h = schema_key_to_hash(x.key);
        if (!h) return;     //malformed schema key - nothing sensible to store
        if (x.erase) {
            _schemas.erase(*h);
            return;         //no event type describes erasing a schema
        }
        auto &ref = _schemas[*h] = std::move(x.value);
        _watcher(ReplicatorEvent{.type = Type::put_schema, .value = ref, .schema_hash = *h,
                                 .key = x.key, .is_schema = true});
    } else if (x.erase) {
        _storage.erase(x.key);
        _watcher(ReplicatorEvent{.type = Type::erase_key, .key = x.key, .erase = true});
    } else {
        auto [iter, ins] = _storage.insert_or_assign(std::move(x.key), std::move(x.value));
        _watcher(ReplicatorEvent{.type = Type::put_key_value, .value = iter->second,
                                 .key = iter->first});
    }
}
```

- [ ] **Step 9: Rewrite `LevelDBStorage::ReplicatorHandler::emit` in `src/quarkbot/leveldb/leveldb_storage.cpp`**

Replace `ReplicatorHandler::Put`, `ReplicatorHandler::Delete` and `emit` (lines 352-369) with:

```cpp
void LevelDBStorage::ReplicatorHandler::Put(const leveldb::Slice& key, const leveldb::Slice& value) {
    emit(key, slice2string_view(value), false);
}
void LevelDBStorage::ReplicatorHandler::Delete(const leveldb::Slice& key) {
    emit(key, {}, true);
}

void LevelDBStorage::ReplicatorHandler::emit(const leveldb::Slice &key, std::string_view value, bool erase) {
    using Type = ReplicatorEvent::Type;
    auto k = slice2string_view(key);
    if (k.empty()) return;
    auto kid = static_cast<std::uint8_t>(k[0]);
    bool is_schema = kid == schema_keyspace;
    auto logical = k.substr(1);

    if (is_schema) {
        //no IStorageTransaction operation erases a schema and delete_storage never
        //touches the schema keyspace, so a delete here has no event type and is dropped
        if (erase) return;
        auto h = schema_key_to_hash(logical);
        if (!h) return;
        repl(ReplicatorEvent{.type = Type::put_schema, .value = value, .schema_hash = *h,
                             .key = logical, .erase = erase, .is_schema = true});
        return;
    }

    //a data key ends with '\0' plus the 16 byte RecordKey; a last-revision pointer is
    //the bare name. A name whose own tail looks like that suffix would be misread, which
    //is inherent to the layout - variable names are identifiers in practice.
    const auto suffix = recordkey_string_size + 1;
    if (logical.size() > suffix && logical[logical.size() - suffix] == '\0') {
        repl(ReplicatorEvent{
            .type = erase?Type::erase_key:Type::put_key_value,
            .name = logical.substr(0, logical.size() - suffix),
            .recordkey = string_to_record_key(logical.substr(logical.size() - recordkey_string_size)),
            .value = value, .key = logical, .erase = erase});
        return;
    }

    if (erase) {
        repl(ReplicatorEvent{.type = Type::erase_latest, .name = logical,
                             .key = logical, .erase = true});
        return;
    }
    //the pointer keeps the newest revision in its *value*, not in its key
    if (value.size() != recordkey_string_size) return;
    repl(ReplicatorEvent{.type = Type::update_latest, .name = logical,
                         .recordkey = string_to_record_key(value),
                         .value = value, .key = logical});
}
```

`schema_key_to_hash` comes from `mem_storage.hpp`; add `#include "../common/mem_storage.hpp"` to `leveldb_storage.cpp` if it is not already included.

- [ ] **Step 10: Drop the unused `keyspace_id` from `ReplicatorHandler`**

In `src/quarkbot/leveldb/leveldb_storage.hpp`, `emit` takes the keyspace from the key's first byte, so the member is never read. Replace the class with:

```cpp
        class ReplicatorHandler final: public leveldb::WriteBatch::Handler {
        public:
            explicit ReplicatorHandler(Replicator &repl):repl(repl) {}
            virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value) override;
            virtual void Delete(const leveldb::Slice& key) override;
        protected:
            Replicator &repl;

            void emit(const leveldb::Slice &key, std::string_view value, bool erase);
        };
```

and in `leveldb_storage.cpp` change `LevelDBStorage::get_replicator()` to `return ReplicatorHandler{_watcher};`.

- [ ] **Step 11: Fix the remaining construction sites so the tree compiles**

`src/quarkbot/common/storage_msgbus_replicator.cpp` — in `replicate_from_message`, replace the positional `trn.put(Storage::ReplicatorEvent{key, value, erase, schema});` with a designated initialiser carrying the binary form plus a type derived from the flags. This is transitional; the wire-format task replaces it wholesale.

```cpp
    using Type = Storage::ReplicatorEvent::Type;
    trn.put(Storage::ReplicatorEvent{
        .type = schema?Type::put_schema:(erase?Type::erase_key:Type::put_key_value),
        .value = value, .key = key, .erase = erase, .is_schema = schema
    });
```

`src/tests/leveldb_storage_test.cpp` — extend `CapturedEvent` and `EventLog` (lines 445-471) to carry the typed fields while keeping the binary ones the existing assertions use:

```cpp
///Collected copy of a ReplicatorEvent - the event itself only borrows its buffers
struct CapturedEvent {
    IStorage::ReplicatorEvent::Type type;
    std::string name;
    RecordKey recordkey;
    std::string value;
    srl::SchemaHash schema_hash;
    std::string key;
    bool erase;
    bool is_schema;
};

class EventLog {
public:
    IStorage::Replicator::Connection attach(const PStorage &storage) {
        auto conn = IStorage::Replicator::create_connection(
            [this](const IStorage::ReplicatorEvent &ev) noexcept {
                events.push_back(CapturedEvent{ev.type, std::string(ev.name), ev.recordkey,
                                               std::string(ev.value), ev.schema_hash,
                                               std::string(ev.key), ev.erase, ev.is_schema});
            });
        storage->add_replicator(conn);
        return conn;
    }
    ///apply everything collected so far to another storage, in order
    void replay_into(const PStorage &target) const {
        auto tx = target->write();
        for (const auto &ev: events) {
            tx->put(IStorage::ReplicatorEvent{
                .type = ev.type, .name = ev.name, .recordkey = ev.recordkey,
                .value = ev.value, .schema_hash = ev.schema_hash,
                .key = ev.key, .erase = ev.erase, .is_schema = ev.is_schema});
        }
        tx->commit();
    }
    void clear() {events.clear();}
    std::vector<CapturedEvent> events;
};
```

`src/tests/mem_storage_test.cpp` — the lambda in `forward()` (line 264) takes the event by value; change its parameter to `const IStorage::ReplicatorEvent &ev`.

- [ ] **Step 12: Run the mem storage test to verify it passes**

```bash
cmake --build build --target test_mem_storage_test -j$(nproc) && ./build/tests/test_mem_storage_test
```

Expected: PASS, including the three new tests.

- [ ] **Step 13: Write the failing LevelDB emission tests**

In `src/tests/leveldb_storage_test.cpp`, replace `test_replicated_key_is_logical` (lines 534-561) with the typed version and add the decomposition test. Update `main()`: replace `test_replicated_key_is_logical();` with `test_replicated_event_is_typed();` and add `test_erase_variable_decomposes();`.

```cpp
void test_replicated_event_is_typed() {
    using Type = IStorage::ReplicatorEvent::Type;
    TempDB tmp("repl_typed");
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

    int data = 0, pointer = 0, schemas = 0;
    for (const auto &ev: log.events) {
        if (ev.type == Type::put_schema) {
            ++schemas;
            CHECK_EQUAL(ev.schema_hash, srl::SchemaHash{0xABCD});
            CHECK_EQUAL(ev.value, "schema-blob");
        } else if (ev.type == Type::put_key_value) {
            ++data;
            CHECK_EQUAL(ev.name, "alpha");
            CHECK(ev.recordkey == RecordKey{1,0});
            CHECK_EQUAL(ev.value, "a1");
        } else if (ev.type == Type::update_latest) {
            ++pointer;
            CHECK_EQUAL(ev.name, "alpha");
            CHECK(ev.recordkey == RecordKey{1,0});
        }
    }
    CHECK_EQUAL(data, 1);
    CHECK_EQUAL(pointer, 1);
    CHECK_EQUAL(schemas, 1);
}

void test_erase_variable_decomposes() {
    using Type = IStorage::ReplicatorEvent::Type;
    TempDB tmp("erase_decompose");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    auto tx = storage->write();
    tx->put("alpha", {1,0}, "a1");
    tx->put("alpha", {2,0}, "a2");
    tx->commit();

    EventLog log;
    auto conn = log.attach(storage);
    tx = storage->write();
    tx->erase("alpha");
    tx->commit();

    // LevelDB deletes record by record, so the batch never carries the bulk intent
    int keys = 0, latest = 0;
    for (const auto &ev: log.events) {
        CHECK(ev.type != Type::erase_name);
        if (ev.type == Type::erase_key) {
            ++keys;
            CHECK_EQUAL(ev.name, "alpha");
            CHECK(ev.recordkey == RecordKey{1,0} || ev.recordkey == RecordKey{2,0});
        } else if (ev.type == Type::erase_latest) {
            ++latest;
            CHECK_EQUAL(ev.name, "alpha");
        }
    }
    CHECK_EQUAL(keys, 2);
    CHECK_EQUAL(latest, 1);
}
```

Also update `test_replicator_fires_on_commit` (lines 474-500): replace `for (const auto &ev: log.events) CHECK(!ev.erase);` with `for (const auto &ev: log.events) CHECK(ev.type == Type::put_key_value || ev.type == Type::update_latest);`, replace the final `CHECK(log.events[0].erase);` with `CHECK(log.events[0].type == Type::erase_key);`, and add `using Type = IStorage::ReplicatorEvent::Type;` at the top of the function.

- [ ] **Step 14: Run the LevelDB test to verify it passes**

```bash
cmake --build build --target test_leveldb_storage_test -j$(nproc) && ./build/tests/test_leveldb_storage_test
```

Expected: PASS.

- [ ] **Step 15: Run the full suite**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: build succeeds, every test passes.

- [ ] **Step 16: Commit**

```bash
git add include/quarkbot/abstract/istorage.hpp \
        src/quarkbot/common/mem_storage.hpp \
        src/quarkbot/common/storage_msgbus_replicator.cpp \
        src/quarkbot/leveldb/leveldb_storage.hpp \
        src/quarkbot/leveldb/leveldb_storage.cpp \
        src/tests/mem_storage_test.cpp \
        src/tests/leveldb_storage_test.cpp
git commit -m "$(cat <<'MSG'
ReplicatorEvent: add typed fields and emit them from both backends

Adds Type plus name, recordkey and schema_hash next to the binary key, so
a consumer no longer needs a copy of the backend's private key format.
LevelDB decodes its committed batch, MemStorage emits straight from the
intent its apply() overloads already carry. The binary fields stay
populated until every consumer is migrated.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 2: Rename put(event) to apply(event) and drive it from the typed fields

Turns application into the mirror of emission: each backend composes its physical key from `name` and `recordkey` instead of copying a prebuilt string. Fixes `StorageNamespaceTransaction`, which forwards a replicated event to the root storage without the namespace prefix.

**Files:**
- Modify: `include/quarkbot/abstract/istorage.hpp` (declaration of `put(const ReplicatorEvent &)`)
- Modify: `include/quarkbot/storage.hpp:170` (`StorageTransaction::put(const Storage::ReplicatorEvent &)`)
- Modify: `include/quarkbot/storage_namespace.hpp:89-91` (`StorageNamespaceTransaction`)
- Modify: `src/quarkbot/common/mem_storage.hpp` (`OpReplicate`, `MemStorageTransaction::put(event)`, `MemStorage::apply(OpReplicate&&)`)
- Modify: `src/quarkbot/leveldb/leveldb_storage.hpp:134`, `src/quarkbot/leveldb/leveldb_storage.cpp:340-350`
- Modify: `src/quarkbot/common/storage_msgbus_replicator.cpp` (call site only)
- Test: `src/tests/mem_storage_test.cpp`
- Test: `src/tests/leveldb_storage_test.cpp`

**Interfaces:**
- Consumes: `IStorage::ReplicatorEvent` with its `Type` enum and typed fields; `CapturedEvent` / `EventLog` in both test files (all from Task 1).
- Produces:
  - `virtual void IStorageTransaction::apply(const IStorage::ReplicatorEvent &event) = 0;` replacing `put(const IStorage::ReplicatorEvent &)`
  - `void StorageTransaction::apply(const Storage::ReplicatorEvent &ev)` on the wrapper
  - `MemStorageTransaction::OpReplicate` = `{IStorage::ReplicatorEvent::Type type; std::string name; RecordKey recordkey; std::string value; srl::SchemaHash schema_hash;}`

- [ ] **Step 1: Write the failing namespace test in `src/tests/mem_storage_test.cpp`**

Add after `test_erase_single_revision_event` and register `test_apply_into_namespace();` in `main()`.

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target test_mem_storage_test -j$(nproc)
```

Expected: FAIL to compile — `no member named 'apply' in 'quarkbot::IStorageTransaction'`.

- [ ] **Step 3: Rename the virtual and the wrapper method**

`include/quarkbot/abstract/istorage.hpp` — replace

```cpp
        ///Replicate from different database
        virtual void put(const IStorage::ReplicatorEvent &event) = 0;
```

with

```cpp
        ///Apply a change described by a replicator event, from this or another database
        /**
            The inverse of IStorage::Replicator: the backend composes its own physical
            key from the event's logical fields. Which fields are read depends on
            event.type - see IStorage::ReplicatorEvent.
        */
        virtual void apply(const IStorage::ReplicatorEvent &event) = 0;
```

`include/quarkbot/storage.hpp:170` — rename to `void apply(const Storage::ReplicatorEvent &ev) {_ptr->apply(ev);}`.

- [ ] **Step 4: Rewrite `StorageNamespaceTransaction::put(event)` as a prefixing `apply`**

In `include/quarkbot/storage_namespace.hpp` replace lines 89-91 with:

```cpp
        virtual void apply(const IStorage::ReplicatorEvent &event) override {
            //every other method prefixes; a replicated event must not be the exception,
            //or replication into a namespace writes outside it. Schemas are global and
            //carry no name.
            if (event.type == IStorage::ReplicatorEvent::Type::put_schema) {
                _root_tx->apply(event);
                return;
            }
            IStorage::ReplicatorEvent prefixed = event;
            prefixed.name = add_prefix(event.name);
            _root_tx->apply(prefixed);
        }
```

- [ ] **Step 5: Make `MemStorage` apply from the typed fields**

In `src/quarkbot/common/mem_storage.hpp`, replace the `OpReplicate` declaration (line 42) with:

```cpp
    struct OpReplicate { IStorage::ReplicatorEvent::Type type; std::string name;
                         RecordKey recordkey; std::string value; srl::SchemaHash schema_hash; };
```

Rename the declaration on line 34 from `put(const IStorage::ReplicatorEvent &event)` to `apply(const IStorage::ReplicatorEvent &event)`, and replace the definition near the end of the file with:

```cpp
inline void MemStorageTransaction::apply(const IStorage::ReplicatorEvent &event) {
    _ops.push_back(OpReplicate{event.type, std::string(event.name), event.recordkey,
                               std::string(event.value), event.schema_hash});
}
```

Replace `MemStorage::apply(MemStorageTransaction::OpReplicate &&x)` with:

```cpp
inline void MemStorage::apply(MemStorageTransaction::OpReplicate &&x) {
    using Type = ReplicatorEvent::Type;
    //re-emitted from the stored copies, never from moved-from members, so that
    //cascaded replication (A -> B -> C) forwards the real content
    switch (x.type) {
        case Type::put_schema: {
            auto &ref = _schemas[x.schema_hash] = std::move(x.value);
            auto key = schema_hash_to_key(x.schema_hash);
            _watcher(ReplicatorEvent{.type = Type::put_schema, .value = ref,
                                     .schema_hash = x.schema_hash,
                                     .key = {key.data(), key.size()}, .is_schema = true});
        } break;
        case Type::put_key_value: {
            std::string key = wholeKey(x.name, x.recordkey);
            auto &ref = _storage[key] = std::move(x.value);
            _watcher(ReplicatorEvent{.type = Type::put_key_value, .name = x.name,
                                     .recordkey = x.recordkey, .value = ref, .key = key});
        } break;
        case Type::update_latest: {
            auto rk = record_key_to_string(x.recordkey);
            _storage[x.name] = rk;
            _watcher(ReplicatorEvent{.type = Type::update_latest, .name = x.name,
                                     .recordkey = x.recordkey, .value = rk, .key = x.name});
        } break;
        case Type::erase_key: {
            std::string key = wholeKey(x.name, x.recordkey);
            _storage.erase(key);
            _watcher(ReplicatorEvent{.type = Type::erase_key, .name = x.name,
                                     .recordkey = x.recordkey, .key = key, .erase = true});
        } break;
        case Type::erase_latest:
            _storage.erase(x.name);
            _watcher(ReplicatorEvent{.type = Type::erase_latest, .name = x.name,
                                     .key = x.name, .erase = true});
            break;
        case Type::erase_name:
            apply(MemStorageTransaction::OpErase{std::move(x.name)});
            break;
    }
}
```

`wholeKey(std::string_view, const RecordKey &)` is already declared in `storage_common.hpp`.

- [ ] **Step 6: Make `LevelDBTransaction` apply from the typed fields**

Rename the declaration in `src/quarkbot/leveldb/leveldb_storage.hpp:134` to
`virtual void apply(const IStorage::ReplicatorEvent &event) override;` and replace
`LevelDBTransaction::put(const IStorage::ReplicatorEvent &event)` in
`src/quarkbot/leveldb/leveldb_storage.cpp:340-350` with:

```cpp
void LevelDBTransaction::apply(const IStorage::ReplicatorEvent &event) {
    using Type = IStorage::ReplicatorEvent::Type;
    auto kid = _storage->get_keyspace_id();
    switch (event.type) {
        case Type::put_schema:
            put_schema_binary(event.schema_hash, event.value);
            break;
        case Type::put_key_value:
            //straight into the batch: put() would also write the pointer, which arrives
            //as its own update_latest event
            _batch.Put(build_key(kid, event.name, event.recordkey),
                       {event.value.data(), event.value.size()});
            break;
        case Type::update_latest: {
            auto rw = record_key_to_string(event.recordkey);
            _batch.Put(build_key(kid, event.name), {rw.data(), rw.size()});
        } break;
        case Type::erase_key:
            erase(event.name, event.recordkey);
            break;
        case Type::erase_latest:
            _batch.Delete(build_key(kid, event.name));
            break;
        case Type::erase_name:
            //decomposes into one Delete per record, so it re-emits as erase_key x N
            //plus erase_latest on this transaction's own commit. Note erase() scans the
            //database, not this batch: records written earlier in the same uncommitted
            //transaction are not removed.
            erase(event.name);
            break;
    }
}
```

- [ ] **Step 7: Update the remaining call sites**

- `src/quarkbot/common/storage_msgbus_replicator.cpp` — `trn.put(...)` becomes `trn.apply(...)` in `replicate_from_message`.
- `src/tests/mem_storage_test.cpp` — in `forward()`, `tx->put(ev)` becomes `tx->apply(ev)`.
- `src/tests/leveldb_storage_test.cpp` — in `EventLog::replay_into`, `tx->put(...)` becomes `tx->apply(...)`.

- [ ] **Step 8: Run the mem storage test to verify it passes**

```bash
cmake --build build --target test_mem_storage_test -j$(nproc) && ./build/tests/test_mem_storage_test
```

Expected: PASS, including `test_apply_into_namespace` and the unchanged `test_replication_cascade`.

- [ ] **Step 9: Write the failing LevelDB application tests**

Add to `src/tests/leveldb_storage_test.cpp` and register both in `main()`.

```cpp
void test_apply_erase_name_decomposes() {
    using Type = IStorage::ReplicatorEvent::Type;
    TempDB tmp("apply_erase_name");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    auto tx = storage->write();
    tx->put("alpha", {1,0}, "a1");
    tx->put("alpha", {2,0}, "a2");
    tx->commit();

    EventLog log;
    auto conn = log.attach(storage);
    tx = storage->write();
    tx->apply(IStorage::ReplicatorEvent{.type = Type::erase_name, .name = "alpha"});
    tx->commit();

    CHECK(!storage->get("alpha", RecordKey{1,0}).exists);
    CHECK(!storage->get("alpha", RecordKey{2,0}).exists);
    CHECK(!storage->get("alpha").exists);

    // the bulk intent decomposes into per-record deletions on the way out again
    int keys = 0, latest = 0;
    for (const auto &ev: log.events) {
        CHECK(ev.type != Type::erase_name);
        if (ev.type == Type::erase_key) ++keys;
        if (ev.type == Type::erase_latest) ++latest;
    }
    CHECK_EQUAL(keys, 2);
    CHECK_EQUAL(latest, 1);
}

void test_apply_pointer_and_data_independently() {
    using Type = IStorage::ReplicatorEvent::Type;
    TempDB tmp("apply_independent");
    auto mgr = open_fresh(tmp.path);
    auto storage = mgr.get_storage("s");

    // a data record alone is reachable by its key but does not move the pointer
    auto tx = storage->write();
    tx->apply(IStorage::ReplicatorEvent{.type = Type::put_key_value, .name = "alpha",
                                        .recordkey = {1,0}, .value = "a1"});
    tx->commit();
    CHECK_EQUAL(storage->get("alpha", RecordKey{1,0}).data, "a1");
    CHECK(!storage->get("alpha").exists);

    // the pointer alone moves get(name) without writing data
    tx = storage->write();
    tx->apply(IStorage::ReplicatorEvent{.type = Type::update_latest, .name = "alpha",
                                        .recordkey = {1,0}});
    tx->commit();
    CHECK_EQUAL(storage->get("alpha").data, "a1");

    // erasing the pointer leaves the data record in place
    tx = storage->write();
    tx->apply(IStorage::ReplicatorEvent{.type = Type::erase_latest, .name = "alpha"});
    tx->commit();
    CHECK(!storage->get("alpha").exists);
    CHECK_EQUAL(storage->get("alpha", RecordKey{1,0}).data, "a1");
}
```

- [ ] **Step 10: Run the LevelDB test to verify it passes**

```bash
cmake --build build --target test_leveldb_storage_test -j$(nproc) && ./build/tests/test_leveldb_storage_test
```

Expected: PASS, including the two cross-keyspace replication tests that go through `replay_into`.

- [ ] **Step 11: Run the full suite**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: build succeeds, every test passes.

- [ ] **Step 12: Commit**

```bash
git add include/quarkbot/abstract/istorage.hpp \
        include/quarkbot/storage.hpp \
        include/quarkbot/storage_namespace.hpp \
        src/quarkbot/common/mem_storage.hpp \
        src/quarkbot/common/storage_msgbus_replicator.cpp \
        src/quarkbot/leveldb/leveldb_storage.hpp \
        src/quarkbot/leveldb/leveldb_storage.cpp \
        src/tests/mem_storage_test.cpp \
        src/tests/leveldb_storage_test.cpp
git commit -m "$(cat <<'MSG'
Storage: apply(event) composes keys from the typed fields

Renames IStorageTransaction::put(ReplicatorEvent) to apply() - it was the
only put overload that could also delete - and each backend now builds its
own physical key from name and recordkey. StorageNamespaceTransaction had
forwarded replicated events to the root without add_prefix, writing outside
the namespace; with a typed name that is a one-line fix.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 3: New message-bus wire format with a round-trip test

Replaces the two-character header with a one-byte type and per-type payload, which also removes the reason put replication has never worked. Splits encoding out of the bus plumbing so the format is testable without a `MessageBus` or an `ExecutionWorker`.

**Files:**
- Modify: `src/quarkbot/common/storage_msgbus_replicator.hpp` (format doc, two new declarations)
- Modify: `src/quarkbot/common/storage_msgbus_replicator.cpp:13-101`
- Create: `src/tests/storage_replication_msgbus.cpp`
- Modify: `src/tests/CMakeLists.txt:6-36` (`BASIC_TESTS`)

**Interfaces:**
- Consumes: `IStorage::ReplicatorEvent` with `Type` (Task 1); `StorageTransaction::apply` (Task 2).
- Produces:
  - `std::size_t quarkbot::replication_message_size(const Storage::ReplicatorEvent &ev);`
  - `std::span<char> quarkbot::encode_replication_message(const Storage::ReplicatorEvent &ev, std::span<char> buffer);`
  - `bool quarkbot::replicate_from_message(const std::string_view &msg, StorageTransaction &trn);` (unchanged signature, new format)

- [ ] **Step 1: Write the failing round-trip test**

Create `src/tests/storage_replication_msgbus.cpp`:

```cpp
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
    CHECK(out[0].recordkey == RecordKey{1,2});
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
    CHECK(out[0].recordkey == RecordKey{7,8});
}

void test_erase_key_round_trip() {
    auto out = round_trip({.type = Type::erase_key, .name = "alpha", .recordkey = {3,4}});
    CHECK_EQUAL(out.size(), 1u);
    CHECK(out[0].type == Type::erase_key);
    CHECK_EQUAL(out[0].name, "alpha");
    CHECK(out[0].recordkey == RecordKey{3,4});
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
```

- [ ] **Step 2: Register the test in `src/tests/CMakeLists.txt`**

Add `storage_replication_msgbus.cpp` to the `BASIC_TESTS` set, on the line after `mem_storage_test.cpp`. `quarkbot_backtest` links `quarkbot_impl` publicly and `storage_msgbus_replicator.cpp` belongs to `impl`, so no extra link line is needed.

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build build --target test_storage_replication_msgbus -j$(nproc)
```

Expected: FAIL to compile — `use of undeclared identifier 'replication_message_size'`.

- [ ] **Step 4: Document the format and declare the encoders in `src/quarkbot/common/storage_msgbus_replicator.hpp`**

Replace the doc comment above `attach_replicator` with the new format and add the two declarations above it. Add `#include <span>`.

```cpp
    ///attach replicator
    /**
        Attaches replicator to a storage and replays events into message bus as messages

        Format of messages:

        <T><payload...>

        <T> = 'S' put_schema     <8B schema hash, big endian> <blob value>
              'P' put_key_value  <blob name> <16B recordkey> <blob value>
              'L' update_latest  <blob name> <16B recordkey>
              'K' erase_key      <blob name> <16B recordkey>
              'R' erase_latest   <blob name>
              'N' erase_name     <blob name>

        <blob>      = <size><bytes>, size variable length big endian with continuation bit 7
        <recordkey> = 16 bytes big endian, the same encoding a physical key uses

        @param storage storage
        @param bus a message bus
        @param target target name (receiver)
        @return connection which must be held to keep replication alive. To stop
        replication simply drop the return value
    */

    ///Number of bytes encode_replication_message needs for this event
    std::size_t replication_message_size(const Storage::ReplicatorEvent &ev);

    ///Encodes event into the wire format
    /**
        @param ev event to encode
        @param buffer destination, at least replication_message_size(ev) bytes
        @return the written prefix of buffer
    */
    std::span<char> encode_replication_message(const Storage::ReplicatorEvent &ev, std::span<char> buffer);
```

- [ ] **Step 5: Implement the encoder in `src/quarkbot/common/storage_msgbus_replicator.cpp`**

Replace line 12 (`repl_content_type`) and lines 33-61 (`replicate_with_buffer`, `attach_replicator`). Leave `write_size_2`, `write_size` and `write_blob` (lines 14-31) untouched — `encode_replication_message` calls `write_blob`. Add `#include "storage_common.hpp"`, `#include "quarkbot/utils/bigendian.hpp"` and `#include <span>`.

```cpp
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

std::size_t replication_message_size(const Storage::ReplicatorEvent &ev) {
    return repl_fixed_overhead + ev.name.size() + ev.value.size() + sizeof(srl::SchemaHash);
}

std::span<char> encode_replication_message(const Storage::ReplicatorEvent &ev, std::span<char> buffer) {
    using Type = Storage::ReplicatorEvent::Type;
    auto *begin = reinterpret_cast<std::uint8_t *>(buffer.data());
    auto *iter = begin;
    *iter++ = static_cast<std::uint8_t>(type_to_wire(ev.type));
    switch (ev.type) {
        case Type::put_schema:
            big_endian_binarize(ev.schema_hash, iter);
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
```

`write_blob` takes a `std::uint8_t *`; keep that and cast at the boundary as shown. `big_endian_binarize` comes from `quarkbot/utils/bigendian.hpp` — add that include. It writes `sizeof(T)` bytes through the iterator it is given and does not return it, hence the explicit `iter += sizeof(srl::SchemaHash)`.

- [ ] **Step 6: Implement the parser**

Replace `replicate_from_message` (lines 81-101) with a version that validates fixed-width fields:

```cpp
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

    Storage::ReplicatorEvent ev{.type = Type::put_key_value};
    switch (t) {
        case 'S': {
            auto bin = extract_fixed(iter, end, sizeof(srl::SchemaHash));
            if (!bin) return false;
            ev.type = Type::put_schema;
            big_endian_unbinarize(ev.schema_hash, bin->begin());
            ev.value = extract_blob(iter, end);
        } break;
        case 'P':
            ev.type = Type::put_key_value;
            ev.name = extract_blob(iter, end);
            if (!read_recordkey(ev.recordkey)) return false;
            ev.value = extract_blob(iter, end);
            break;
        case 'L':
        case 'K':
            ev.type = t == 'L'?Type::update_latest:Type::erase_key;
            ev.name = extract_blob(iter, end);
            if (!read_recordkey(ev.recordkey)) return false;
            break;
        case 'R':
        case 'N':
            ev.type = t == 'R'?Type::erase_latest:Type::erase_name;
            ev.name = extract_blob(iter, end);
            break;
        default:
            return false;
    }
    trn.apply(ev);
    return true;
}
```

Add `#include <optional>` if missing. `big_endian_unbinarize` is the exact inverse of the `big_endian_binarize` used by the encoder, so the two agree by construction — the same pair `record_key_to_string` / `string_to_record_key` is built on.

- [ ] **Step 7: Run the test to verify it passes**

```bash
cmake --build build --target test_storage_replication_msgbus -j$(nproc) && ./build/tests/test_storage_replication_msgbus
```

Expected: PASS, all seven tests.

- [ ] **Step 8: Run the full suite**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: build succeeds, every test passes, including the new `tests/storage_replication_msgbus`.

- [ ] **Step 9: Commit**

```bash
git add src/quarkbot/common/storage_msgbus_replicator.hpp \
        src/quarkbot/common/storage_msgbus_replicator.cpp \
        src/tests/storage_replication_msgbus.cpp \
        src/tests/CMakeLists.txt
git commit -m "$(cat <<'MSG'
Replication over message bus: typed wire format and a round-trip test

One type byte and a per-type payload replace the <put|erase><row|schema>
header. That header also carried a typo - the second test read ch2 where it
meant ch1, so every put message was dropped and only erases replicated.
There was no test covering it; there is one now, per event type, plus
refusal of messages that cut a recordkey or schema hash short.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 4: Migrate the reporting consumers to the typed fields

The four replicator consumers stop parsing the binary key. Each keeps its observable output, except where today's output is a side effect of reading a binary key as text.

**Files:**
- Modify: `src/quarkbot/backtest/json_report.cpp:146-183`
- Modify: `src/quarkbot/backtest/var_inspector.cpp:11-29`
- Modify: `src/quarkbot/backtest/persistent_reporter.cpp:36-61`
- Modify: `src/quarkbot/backtest/simple_stdio_debugger.cpp:38-49`

**Interfaces:**
- Consumes: `IStorage::ReplicatorEvent` with `Type`, `name`, `recordkey`, `schema_hash` (Task 1).
- Produces: nothing new. `schema_key_to_hash` loses its last three call sites here; the helper itself is deleted in Task 5.

- [ ] **Step 1: Rewrite the `json_report` handler**

In `src/quarkbot/backtest/json_report.cpp`, replace the lambda body inside `attach_storage` (lines 149-182) with:

```cpp
        _storage_report = storage.add_replicator([this, schema_map](const Storage::ReplicatorEvent &ev)mutable noexcept{
            using Type = Storage::ReplicatorEvent::Type;
            if (ev.type == Type::put_schema) {
                try {
                    schema_map[ev.schema_hash] = Json::from_string(ev.value);
                } catch (...) {
                    //an unparseable schema just means values render as binary
                }
            } else if (ev.type == Type::put_key_value) {
                srl::SchemaHash sch;
                Json jval;
                extract_srl(ev.value, sch, sch);
                auto schiter = schema_map.find(sch);
                if (schiter == schema_map.end()) {
                    jval = binary_content(ev.value);
                } else {
                    auto arch = srl::string_deserializer(ev.value);
                    jval = srl::deserialize_from_schema(schiter->second, arch, get_desrl_resolver());
                }
                out(Event::var_update,{
                    {"name", ev.name},
                    {"rev", {ev.recordkey.ordered, ev.recordkey.random}},
                    {"val",std::move(jval)}
                });
            }
        });
```

- [ ] **Step 2: Rewrite the `var_inspector` handler**

In `src/quarkbot/backtest/var_inspector.cpp`, replace the lambda body inside `attach_storage` (lines 14-28) with:

```cpp
        _conn = _storage.add_replicator([this](const Storage::ReplicatorEvent &ev) noexcept {
            using Type = Storage::ReplicatorEvent::Type;
            std::scoped_lock _(_mx);
            if (ev.type == Type::put_schema) {
                try {
                    _schema_cache[ev.schema_hash] = Json::from_string(ev.value);
                } catch (const std::exception &e) {
                    logWarning("Failed to parse schema {}: {}", ev.schema_hash, e.what());
                }
            } else if (ev.type == Type::put_key_value) {
                //the record itself, not the last-revision pointer: a put with
                //UpdateLastRevision::disable emits no pointer event but still updates
                _updated_vars.insert(std::string(ev.name));
            }
        });
```

- [ ] **Step 3: Rewrite the `persistent_reporter` handler**

In `src/quarkbot/backtest/persistent_reporter.cpp`, replace the lambda returned by `reporter_replicator` (lines 38-60) with:

```cpp
    return [&out, schema_cache](const Storage::ReplicatorEvent &ev)mutable noexcept{
        using Type = Storage::ReplicatorEvent::Type;
        try {
            if (ev.type == Type::put_schema) {
                schema_cache.emplace(ev.schema_hash, Json::from_string(ev.value));
            } else if (ev.type == Type::put_key_value) {
                srl::SchemaHash hash;
                std::nullptr_t dummy;
                extract_srl(ev.value, dummy, hash);
                auto schiter = schema_cache.find(hash);
                Json jval;
                if (schiter == schema_cache.end()) {
                    jval = binary_content(ev.value);
                } else {
                    auto arch = srl::string_deserializer(ev.value);
                    jval = srl::deserialize_from_schema(schiter->second, arch,get_desrl_resolver());
                }
                auto now = ExecutionWorker::current().now();
                std::println(out, "{:%Y-%m-%d %H:%M:%S}\t{}\t{}", now, ev.name, jval.to_string());
            }
        } catch (const std::exception &e) {
            logError("Execption in variable renderer: {}, key={}", e.what(), ev.name);
        }
    };
```

- [ ] **Step 4: Rewrite the `simple_stdio_debugger` watchpoint handler**

In `src/quarkbot/backtest/simple_stdio_debugger.cpp`, replace the lambda body inside the constructor (lines 40-48) with:

```cpp
            _watcher = _store.add_replicator([this](const Storage::ReplicatorEvent &ev) noexcept {
                std::scoped_lock lock(_mx);
                if (ev.type == Storage::ReplicatorEvent::Type::put_key_value) {
                    auto it = _watches.find(ev.name);
                    if (it != _watches.end() && it->second) {
                        _control->set_running(false);
                    }
                }
            });
```

- [ ] **Step 5: Run the full suite to verify nothing regressed**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: build succeeds, every test passes. `tests/json_report` covers the first handler directly; the other three have no dedicated test, so the build plus the full suite is the check.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/backtest/json_report.cpp \
        src/quarkbot/backtest/var_inspector.cpp \
        src/quarkbot/backtest/persistent_reporter.cpp \
        src/quarkbot/backtest/simple_stdio_debugger.cpp
git commit -m "$(cat <<'MSG'
Reporting consumers read the typed replicator event

Each of the four handlers stops reconstructing the backend's key layout.
Three of them were only working by accident: var_inspector filled its
updated set with binary keys and never reported a put made with
UpdateLastRevision::disable, persistent_reporter printed a binary name plus
a hex line per pointer update, and simple_stdio_debugger matched a
watchpoint only via the pointer event.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 5: Remove the transitional fields and the dead helpers

Nothing reads `key`, `erase` or `is_schema` any more. Removing them also removes the last use of the schema-hash-as-string helpers.

**Files:**
- Modify: `include/quarkbot/abstract/istorage.hpp` (the transitional block)
- Modify: `src/quarkbot/common/mem_storage.hpp` (drop legacy population; delete `schema_hash_to_key` / `schema_key_to_hash`)
- Modify: `src/quarkbot/leveldb/leveldb_storage.cpp` (drop legacy population; inline the schema-hash decode)
- Modify: `src/tests/leveldb_storage_test.cpp` (drop the legacy members of `CapturedEvent`)

**Interfaces:**
- Consumes: everything from Tasks 1-4.
- Produces: `IStorage::ReplicatorEvent` in its final shape — `Type type; std::string_view name; RecordKey recordkey; std::string_view value; srl::SchemaHash schema_hash;` and nothing else.

- [ ] **Step 1: Confirm nothing reads the transitional fields**

```bash
cd /home/ondra/vscode/quarkbot_core
grep -rn "\.is_schema\|\.erase\b\|ev\.key\|event\.key" --include=*.cpp --include=*.hpp include src | grep -v "_storage.erase\|_batch.Delete\|_schemas.erase\|\.erase(" 
```

Expected: hits only inside `mem_storage.hpp`, `leveldb_storage.cpp` and `leveldb_storage_test.cpp`, where the fields are *written*. Any other hit must be migrated before continuing.

- [ ] **Step 2: Delete the transitional block from the struct**

In `include/quarkbot/abstract/istorage.hpp`, remove the `///@{ ... ///@}` block declaring `key`, `erase` and `is_schema`, leaving `schema_hash` as the last member.

- [ ] **Step 3: Run the build to find every writer**

```bash
cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head -30
```

Expected: errors at the `.key = `, `.erase = ` and `.is_schema = ` initialisers in `mem_storage.hpp`, `leveldb_storage.cpp` and `leveldb_storage_test.cpp`.

- [ ] **Step 4: Drop the legacy initialisers**

Remove every `.key = ...`, `.erase = ...` and `.is_schema = ...` from the `ReplicatorEvent{...}` initialisers in `src/quarkbot/common/mem_storage.hpp` and `src/quarkbot/leveldb/leveldb_storage.cpp`. In `mem_storage.hpp`, `MemStorage::apply(OpPut&&)` no longer needs `erased_key` as a separate variable — keep it only where it is still used to erase from `_storage`.

In `src/tests/leveldb_storage_test.cpp`, remove `key`, `erase` and `is_schema` from `CapturedEvent`, from the push_back in `EventLog::attach` and from the event built in `EventLog::replay_into`.

- [ ] **Step 5: Delete the schema-hash string helpers**

In `src/quarkbot/common/mem_storage.hpp`, delete `schema_hash_to_key` and `schema_key_to_hash` together with their doc comment. `MemStorage::apply(OpPutSchema&&)` and `apply(OpReplicate&&)` no longer build a binary schema key.

In `src/quarkbot/leveldb/leveldb_storage.cpp`, `ReplicatorHandler::emit` used `schema_key_to_hash`; replace that call with an inline decode:

```cpp
    if (is_schema) {
        //no IStorageTransaction operation erases a schema and delete_storage never
        //touches the schema keyspace, so a delete here has no event type and is dropped
        if (erase) return;
        if (logical.size() != sizeof(srl::SchemaHash)) return;
        std::array<char, sizeof(srl::SchemaHash)> bin;
        std::copy(logical.begin(), logical.end(), bin.begin());
        repl(ReplicatorEvent{.type = Type::put_schema, .value = value,
                             .schema_hash = std::bit_cast<srl::SchemaHash>(bin)});
        return;
    }
```

Remove the `#include "../common/mem_storage.hpp"` from `leveldb_storage.cpp` if it was added in Task 1 and is now unused.

- [ ] **Step 6: Run the full suite**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: build succeeds, every test passes.

- [ ] **Step 7: Commit**

```bash
git add include/quarkbot/abstract/istorage.hpp \
        src/quarkbot/common/mem_storage.hpp \
        src/quarkbot/leveldb/leveldb_storage.cpp \
        src/tests/leveldb_storage_test.cpp
git commit -m "$(cat <<'MSG'
ReplicatorEvent: drop the transitional binary fields

Every producer and consumer now works from the typed fields, so key, erase
and is_schema go, and with them schema_hash_to_key/schema_key_to_hash -
they existed only to carry a SchemaHash as a byte string.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 6: MemStorage reports a bulk erase as one erase_name

The last behaviour change. `MemStorage::apply(OpErase&&)` knows the caller asked for `erase(variable_name)`, so it says so once instead of describing the removal record by record. A LevelDB source still decomposes, because its batch no longer carries the intent.

**Files:**
- Modify: `src/quarkbot/common/mem_storage.hpp` (`MemStorage::apply(OpErase&&)`)
- Test: `src/tests/mem_storage_test.cpp` (replace `test_erase_replicates_as_erase`)

**Interfaces:**
- Consumes: `IStorage::ReplicatorEvent` in its final shape (Task 5); `EventLog` (Task 1).
- Produces: no API change.

- [ ] **Step 1: Replace `test_erase_replicates_as_erase` with the new expectation**

In `src/tests/mem_storage_test.cpp`, delete `test_erase_replicates_as_erase` (and its call in `main()`) and add, registering `test_erase_emits_erase_name();` in its place:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --target test_mem_storage_test -j$(nproc) && ./build/tests/test_mem_storage_test
```

Expected: FAIL — `log.events.size() == 3` (one `erase_latest` plus two `erase_key`), not 1.

- [ ] **Step 3: Emit a single `erase_name`**

In `src/quarkbot/common/mem_storage.hpp`, replace `MemStorage::apply(MemStorageTransaction::OpErase &&x)` with:

```cpp
inline void MemStorage::apply(MemStorageTransaction::OpErase &&x) {
    using Type = ReplicatorEvent::Type;
    _storage.erase(x.variable);
    std::string beg = x.variable + '\0';
    std::string end = x.variable + '\x01';
    _storage.erase(_storage.lower_bound(beg), _storage.lower_bound(end));
    //the caller asked for the whole variable, so say that once - a replica backed by a
    //relational store turns it into a single DELETE ... WHERE name = ?. A LevelDB source
    //cannot do this: its committed batch holds per-record deletions and nothing else.
    _watcher(ReplicatorEvent{.type = Type::erase_name, .name = x.variable});
}
```

The event is emitted after the removal so that `name` still refers to the op's own string, which outlives the call.

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build --target test_mem_storage_test -j$(nproc) && ./build/tests/test_mem_storage_test
```

Expected: PASS, including `test_replication_cascade`, whose `erase("var")` now travels as one event and must still clear the replicas.

- [ ] **Step 5: Run the full suite**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build
```

Expected: build succeeds, every test passes.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/common/mem_storage.hpp src/tests/mem_storage_test.cpp
git commit -m "$(cat <<'MSG'
MemStorage: report a bulk erase as a single erase_name

erase(variable_name) is one logical operation and MemStorage knows it, so
it emits one event instead of describing the removal record by record. A
LevelDB source still decomposes - its committed batch carries per-record
deletions and no intent - which is the asymmetry erase_name and
erase_latest exist to express.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```
