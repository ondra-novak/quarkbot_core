# MemStorage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `MemStorage` — a header-only in-memory implementation of `IStorage`/`IStorageTransaction` for use in tests.

**Architecture:** Two separate `unordered_map` namespaces (plain and sequence). Plain keys store last value only; sequence keys store full history in a `std::map<Revision, pair<bool,string>>`. Transaction buffers operations as a `vector<variant<...>>` and applies them atomically on `commit()`.

**Tech Stack:** C++23, header-only, no external deps beyond `ifc/storage.hpp` and STL.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/impl/mem_storage.hpp` | Create | Full implementation (header-only) |
| `src/tests/mem_storage_test.cpp` | Create | All tests |
| `src/tests/CMakeLists.txt` | Modify | Register the new test executable |

---

### Task 1: Scaffold — empty classes + CMake wiring

**Files:**
- Create: `src/impl/mem_storage.hpp`
- Create: `src/tests/mem_storage_test.cpp`
- Modify: `src/tests/CMakeLists.txt`

- [ ] **Step 1: Create `src/impl/mem_storage.hpp` with class skeletons**

```cpp
#pragma once

#include "ifc/storage.hpp"
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace quarkbot {

class MemStorage;

class MemStorageTransaction final : public IStorageTransaction {
public:
    explicit MemStorageTransaction(MemStorage &storage) : _storage(storage) {}

    Revision put(Key key, std::string_view value_blob) override;
    Revision erase(Key key) override;
    void prune_history(Key key, Revision from, Revision to) override;
    void commit() override;

private:
    struct OpPut   { std::string key_name; bool sequence; std::string data; };
    struct OpErase { std::string key_name; bool sequence; };
    struct OpPrune { std::string key_name; bool sequence; Revision from, to; };
    using Op = std::variant<OpPut, OpErase, OpPrune>;

    std::vector<Op> _ops;
    MemStorage &_storage;
};

class MemStorage final : public IStorage {
public:
    Value get(Key key) const override;
    Value get(Key key, Revision rev) const override;
    std::vector<std::string> get_all_keys(const Key &filter) const override;
    PStorageTransaction write() override;

    Revision apply_put(Key key, std::string_view data);
    Revision apply_erase(Key key);
    void apply_prune(Key key, Revision from, Revision to);
    Revision next_seq_rev(std::string_view name) const;

private:
    struct SeqEntry {
        Revision next_rev = 1;
        std::map<Revision, std::pair<bool, std::string>> history;
    };

    std::unordered_map<std::string, std::string> _plain;
    std::unordered_map<std::string, SeqEntry> _seq;
};

// --- MemStorage ---

inline IStorage::Value MemStorage::get(Key /*key*/) const { return {0, false, {}}; }
inline IStorage::Value MemStorage::get(Key /*key*/, Revision /*rev*/) const { return {0, false, {}}; }
inline std::vector<std::string> MemStorage::get_all_keys(const Key &/*filter*/) const { return {}; }
inline PStorageTransaction MemStorage::write() { return std::make_unique<MemStorageTransaction>(*this); }
inline IStorage::Revision MemStorage::apply_put(Key /*key*/, std::string_view /*data*/) { return 0; }
inline IStorage::Revision MemStorage::apply_erase(Key /*key*/) { return 0; }
inline void MemStorage::apply_prune(Key /*key*/, Revision /*from*/, Revision /*to*/) {}
inline IStorage::Revision MemStorage::next_seq_rev(std::string_view /*name*/) const { return 1; }

// --- MemStorageTransaction ---

inline IStorageTransaction::Revision MemStorageTransaction::put(Key key, std::string_view value_blob) {
    _ops.emplace_back(OpPut{std::string(key.name), key.sequence, std::string(value_blob)});
    return key.sequence ? _storage.next_seq_rev(key.name) : 0;
}
inline IStorageTransaction::Revision MemStorageTransaction::erase(Key key) {
    _ops.emplace_back(OpErase{std::string(key.name), key.sequence});
    return key.sequence ? _storage.next_seq_rev(key.name) : 0;
}
inline void MemStorageTransaction::prune_history(Key key, Revision from, Revision to) {
    _ops.emplace_back(OpPrune{std::string(key.name), key.sequence, from, to});
}
inline void MemStorageTransaction::commit() {
    for (auto &op : _ops) {
        std::visit([this](auto &o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, OpPut>)
                _storage.apply_put({o.key_name, o.sequence}, o.data);
            else if constexpr (std::is_same_v<T, OpErase>)
                _storage.apply_erase({o.key_name, o.sequence});
            else
                _storage.apply_prune({o.key_name, o.sequence}, o.from, o.to);
        }, op);
    }
    _ops.clear();
}

} // namespace quarkbot
```

- [ ] **Step 2: Create `src/tests/mem_storage_test.cpp`**

```cpp
#include "impl/mem_storage.hpp"
#include "tests/check.h"

int main() {
    return 0;
}
```

- [ ] **Step 3: Add test to `src/tests/CMakeLists.txt`**

In `src/tests/CMakeLists.txt`, add `mem_storage_test.cpp` to `BASIC_TESTS`:

```cmake
set(BASIC_TESTS
    compile_test.cpp
    decimal_test.cpp
    pubsub.cpp
    mem_storage_test.cpp
)
```

- [ ] **Step 4: Verify it compiles**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -5
```

Expected: no errors, binary at `build/tests/tests_mem_storage_test`.

- [ ] **Step 5: Commit**

```bash
git add src/impl/mem_storage.hpp src/tests/mem_storage_test.cpp src/tests/CMakeLists.txt
git commit -m "feat: scaffold MemStorage in-memory IStorage implementation"
```

---

### Task 2: Non-sequence `put` and `get`

**Files:**
- Modify: `src/tests/mem_storage_test.cpp`
- Modify: `src/impl/mem_storage.hpp`

- [ ] **Step 1: Write failing tests in `mem_storage_test.cpp`**

```cpp
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

int main() {
    test_plain_put_get();
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: `FAILED: v2.exists` (get returns false, stub not implemented yet).

- [ ] **Step 3: Implement `get` (non-sequence) and `apply_put` (non-sequence) in `mem_storage.hpp`**

Replace the stub implementations with:

```cpp
inline IStorage::Value MemStorage::get(Key key) const {
    if (!key.sequence) {
        auto it = _plain.find(std::string(key.name));
        if (it == _plain.end()) return {0, false, {}};
        return {0, true, it->second};
    }
    auto it = _seq.find(std::string(key.name));
    if (it == _seq.end() || it->second.history.empty()) return {0, false, {}};
    const auto &entry = it->second;
    auto last = entry.history.rbegin();
    return {last->first, last->second.first, last->second.second};
}

inline IStorage::Revision MemStorage::apply_put(Key key, std::string_view data) {
    if (!key.sequence) {
        _plain[std::string(key.name)] = std::string(data);
        return 0;
    }
    auto &entry = _seq[std::string(key.name)];
    Revision rev = entry.next_rev++;
    entry.history.emplace(rev, std::make_pair(true, std::string(data)));
    return rev;
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: all `Passed:` lines, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/impl/mem_storage.hpp src/tests/mem_storage_test.cpp
git commit -m "feat: non-sequence put/get for MemStorage"
```

---

### Task 3: Non-sequence `erase` and `get_all_keys`

**Files:**
- Modify: `src/tests/mem_storage_test.cpp`
- Modify: `src/impl/mem_storage.hpp`

- [ ] **Step 1: Add failing tests**

Append to `mem_storage_test.cpp` (before `main`):

```cpp
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
```

Update `main`:

```cpp
int main() {
    test_plain_put_get();
    test_plain_erase();
    test_plain_get_all_keys();
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: `FAILED` at erase or get_all_keys check.

- [ ] **Step 3: Implement `apply_erase` (non-sequence) and `get_all_keys`**

Replace stub implementations:

```cpp
inline IStorage::Revision MemStorage::apply_erase(Key key) {
    if (!key.sequence) {
        _plain.erase(std::string(key.name));
        return 0;
    }
    auto &entry = _seq[std::string(key.name)];
    Revision rev = entry.next_rev++;
    entry.history.emplace(rev, std::make_pair(false, std::string{}));
    return rev;
}

inline std::vector<std::string> MemStorage::get_all_keys(const Key &filter) const {
    std::vector<std::string> result;
    if (!filter.sequence) {
        for (const auto &[k, v] : _plain) {
            if (k.starts_with(filter.name)) result.push_back(k);
        }
    } else {
        for (const auto &[k, e] : _seq) {
            if (k.starts_with(filter.name)) result.push_back(k);
        }
    }
    return result;
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: all `Passed:`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/impl/mem_storage.hpp src/tests/mem_storage_test.cpp
git commit -m "feat: non-sequence erase and get_all_keys for MemStorage"
```

---

### Task 4: Sequence `put` and `get` (latest value)

**Files:**
- Modify: `src/tests/mem_storage_test.cpp`
- Modify: `src/impl/mem_storage.hpp`

- [ ] **Step 1: Add failing tests**

Append before `main`:

```cpp
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
    CHECK_EQUAL(v3.rev, 3u);
    CHECK_EQUAL(v3.data, "data3");
}
```

Update `main`:

```cpp
int main() {
    test_plain_put_get();
    test_plain_erase();
    test_plain_get_all_keys();
    test_seq_put_get_latest();
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: `FAILED` at sequence get check.

- [ ] **Step 3: Implement `apply_put` (sequence branch) and `get` (sequence branch)**

The `get(Key)` implementation already has the sequence branch above. The `apply_put` (sequence branch) is also implemented in Task 2. Verify both branches are in the current `mem_storage.hpp` — they should already be there from Task 2. Also implement `next_seq_rev`:

```cpp
inline IStorage::Revision MemStorage::next_seq_rev(std::string_view name) const {
    auto it = _seq.find(std::string(name));
    if (it == _seq.end()) return 1;
    return it->second.next_rev;
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: all `Passed:`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/impl/mem_storage.hpp src/tests/mem_storage_test.cpp
git commit -m "feat: sequence put/get (latest) for MemStorage"
```

---

### Task 5: Sequence `get` by revision + `erase` (tombstone)

**Files:**
- Modify: `src/tests/mem_storage_test.cpp`
- Modify: `src/impl/mem_storage.hpp`

- [ ] **Step 1: Add failing tests**

Append before `main`:

```cpp
void test_seq_get_by_revision() {
    MemStorage storage;

    auto tx = storage.write();
    tx->put({"log", true}, "entry1");
    tx->put({"log", true}, "entry2");  // two puts in one tx — second revision is estimated as 1 (UB for rev return), but both are committed
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
```

Update `main`:

```cpp
int main() {
    test_plain_put_get();
    test_plain_erase();
    test_plain_get_all_keys();
    test_seq_put_get_latest();
    test_seq_get_by_revision();
    test_seq_erase_tombstone();
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: `FAILED` at one of the new checks.

- [ ] **Step 3: Implement `get(Key, Revision)` and `apply_erase` (sequence branch)**

Replace stub with:

```cpp
inline IStorage::Value MemStorage::get(Key key, Revision rev) const {
    if (!key.sequence) return get(key);
    auto it = _seq.find(std::string(key.name));
    if (it == _seq.end()) return {0, false, {}};
    const auto &entry = it->second;
    auto hit = entry.history.find(rev);
    if (hit == entry.history.end()) return {0, false, {}};
    return {rev, hit->second.first, hit->second.second};
}
```

The `apply_erase` sequence branch is already in Task 3's implementation. Verify it is in the file.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: all `Passed:`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/impl/mem_storage.hpp src/tests/mem_storage_test.cpp
git commit -m "feat: sequence get-by-revision and erase tombstone for MemStorage"
```

---

### Task 6: `prune_history`

**Files:**
- Modify: `src/tests/mem_storage_test.cpp`
- Modify: `src/impl/mem_storage.hpp`

- [ ] **Step 1: Add failing tests**

Append before `main`:

```cpp
void test_seq_prune_history() {
    MemStorage storage;

    // Build history: revisions 1–5
    auto tx = storage.write();
    for (int i = 1; i <= 5; ++i) {
        tx->put({"hist", true}, "v" + std::to_string(i));
    }
    tx->commit();

    CHECK_EQUAL(storage.get({"hist", true}).rev, 5u);

    // prune revisions 1–3 (keep 4, 5)
    auto txp = storage.write();
    txp->prune_history({"hist", true}, 1, 3);
    txp->commit();

    // 4 and 5 still accessible
    CHECK(storage.get({"hist", true}, 4).exists);
    CHECK_EQUAL(storage.get({"hist", true}, 4).data, "v4");
    CHECK(storage.get({"hist", true}, 5).exists);
    CHECK_EQUAL(storage.get({"hist", true}, 5).data, "v5");

    // 1, 2, 3 are gone
    CHECK(!storage.get({"hist", true}, 1).exists);
    CHECK_EQUAL(storage.get({"hist", true}, 1).rev, 0u);
    CHECK(!storage.get({"hist", true}, 2).exists);
    CHECK(!storage.get({"hist", true}, 3).exists);

    // latest still works
    CHECK_EQUAL(storage.get({"hist", true}).rev, 5u);

    // prune with to > last_rev: last revision is protected
    auto txp2 = storage.write();
    txp2->prune_history({"hist", true}, 4, 999);
    txp2->commit();

    // rev 5 (last) survives
    CHECK(storage.get({"hist", true}, 5).exists);
    CHECK_EQUAL(storage.get({"hist", true}).rev, 5u);

    // rev 4 was pruned
    CHECK(!storage.get({"hist", true}, 4).exists);

    // prune on non-sequence key is a no-op
    auto txs = storage.write();
    txs->put({"plain", false}, "x");
    txs->commit();
    auto txp3 = storage.write();
    txp3->prune_history({"plain", false}, 0, 99);
    txp3->commit();
    CHECK(storage.get({"plain", false}).exists);  // still there
}
```

Update `main`:

```cpp
int main() {
    test_plain_put_get();
    test_plain_erase();
    test_plain_get_all_keys();
    test_seq_put_get_latest();
    test_seq_get_by_revision();
    test_seq_erase_tombstone();
    test_seq_prune_history();
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: `FAILED` at prune check (rev 1 still exists).

- [ ] **Step 3: Implement `apply_prune`**

Replace stub:

```cpp
inline void MemStorage::apply_prune(Key key, Revision from, Revision to) {
    if (!key.sequence) return;
    auto it = _seq.find(std::string(key.name));
    if (it == _seq.end() || it->second.history.empty()) return;
    auto &entry = it->second;
    Revision last_rev = entry.history.rbegin()->first;
    if (last_rev == 0) return;  // safety: empty
    Revision actual_to = std::min(to, last_rev - 1);
    if (from > actual_to) return;
    entry.history.erase(
        entry.history.lower_bound(from),
        entry.history.upper_bound(actual_to)
    );
}
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: all `Passed:`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/impl/mem_storage.hpp src/tests/mem_storage_test.cpp
git commit -m "feat: prune_history for MemStorage"
```

---

### Task 7: `get_all_keys` (sequence) + namespace separation

**Files:**
- Modify: `src/tests/mem_storage_test.cpp`
- Modify: `src/impl/mem_storage.hpp` (verify `get_all_keys` sequence branch — it should already be implemented from Task 3)

- [ ] **Step 1: Add failing tests**

Append before `main`:

```cpp
void test_seq_get_all_keys() {
    MemStorage storage;

    auto tx = storage.write();
    tx->put({"order:A", true}, "a");
    tx->put({"order:B", true}, "b");
    tx->put({"fill:1",  true}, "c");
    tx->commit();

    // prefix filter on sequence keys
    auto keys = storage.get_all_keys({"order:", true});
    CHECK_EQUAL(keys.size(), 2u);
    bool hasA = false, hasB = false;
    for (auto &k : keys) {
        if (k == "order:A") hasA = true;
        if (k == "order:B") hasB = true;
    }
    CHECK(hasA);
    CHECK(hasB);

    // empty prefix = all sequence keys
    auto all = storage.get_all_keys({"", true});
    CHECK_EQUAL(all.size(), 3u);
}

void test_namespace_separation() {
    MemStorage storage;

    // same name "key" in both namespaces — independent
    auto tx = storage.write();
    tx->put({"key", false}, "plain_value");
    tx->put({"key", true},  "seq_value");
    tx->commit();

    auto vp = storage.get({"key", false});
    CHECK(vp.exists);
    CHECK_EQUAL(vp.data, "plain_value");
    CHECK_EQUAL(vp.rev, 0u);

    auto vs = storage.get({"key", true});
    CHECK(vs.exists);
    CHECK_EQUAL(vs.data, "seq_value");
    CHECK_EQUAL(vs.rev, 1u);

    // erasing plain doesn't affect sequence
    auto tx2 = storage.write();
    tx2->erase({"key", false});
    tx2->commit();

    CHECK(!storage.get({"key", false}).exists);
    CHECK(storage.get({"key", true}).exists);

    // get_all_keys respects namespace
    auto plain_keys = storage.get_all_keys({"", false});
    CHECK_EQUAL(plain_keys.size(), 0u);

    auto seq_keys = storage.get_all_keys({"", true});
    CHECK_EQUAL(seq_keys.size(), 1u);
    CHECK_EQUAL(seq_keys[0], "key");
}
```

Update `main`:

```cpp
int main() {
    test_plain_put_get();
    test_plain_erase();
    test_plain_get_all_keys();
    test_seq_put_get_latest();
    test_seq_get_by_revision();
    test_seq_erase_tombstone();
    test_seq_prune_history();
    test_seq_get_all_keys();
    test_namespace_separation();
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: `FAILED` at a sequence get_all_keys or namespace check.

- [ ] **Step 3: Verify `get_all_keys` sequence branch is present**

The sequence branch of `get_all_keys` was already implemented in Task 3. Confirm it reads from `_seq`:

```cpp
} else {
    for (const auto &[k, e] : _seq) {
        if (k.starts_with(filter.name)) result.push_back(k);
    }
}
```

If missing, add it now. No other changes should be needed.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build /home/ondra/workspace/trading_interface/build --target tests_mem_storage_test 2>&1 | tail -3 && /home/ondra/workspace/trading_interface/build/tests/tests_mem_storage_test
```

Expected: all `Passed:`, exit 0.

- [ ] **Step 5: Run all tests to confirm nothing is broken**

```bash
cd /home/ondra/workspace/trading_interface/build && ctest --output-on-failure 2>&1 | tail -20
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/impl/mem_storage.hpp src/tests/mem_storage_test.cpp
git commit -m "feat: sequence get_all_keys and namespace separation tests for MemStorage"
```

---

## Self-Review Notes

**Spec coverage:**
- ✅ Non-sequence: put, get, erase, get_all_keys, revision always 0
- ✅ Sequence: put (auto-increment revision), get latest, get by revision
- ✅ Sequence erase: tombstone with next revision
- ✅ prune_history: range erasure, last revision protected, no-op for non-sequence
- ✅ get_all_keys: prefix filter, sequence/non-sequence selection
- ✅ Namespace separation: same name → independent keys
- ✅ Transaction as write-batch: no reads of own writes, commit is atomic, uncommitted = discarded
- ✅ UB note: two-puts-in-same-tx tested in `test_seq_get_by_revision`

**Types:** `Revision = std::size_t`; all literal comparisons use `u` suffix. `Key::name` is `string_view` — stored by value (`std::string`) in Op structs.

**No placeholders:** all steps have complete code.
