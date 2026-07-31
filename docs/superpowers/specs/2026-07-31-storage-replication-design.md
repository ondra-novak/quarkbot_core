# Storage replication: portable ReplicatorEvent + LevelDB storage test

Date: 2026-07-31

## Background

Commit `51a2010` replaced the pre-commit write watcher with a post-commit
replicator. `LevelDBTransaction::commit()` now writes the `leveldb::WriteBatch`
first, then replays it through `LevelDBStorage::ReplicatorHandler` (a
`leveldb::WriteBatch::Handler`) to emit `IStorage::ReplicatorEvent` per
physical record. `MemStorage` emits equivalent events from its `apply()`
overloads.

Both backends share the same *inner* key encoding:

```
<variable_name> '\0' <big-endian uint64 ordered> <big-endian uint64 random>
```

plus a "last revision pointer" record keyed by `<variable_name>` alone, whose
value is the 16-byte encoded `RecordKey` of the newest revision.

LevelDB additionally prefixes every physical key with a 1-byte keyspace id, and
stores schemas under the reserved keyspace `0xFE`. `MemStorage` has no keyspace
prefix and keeps schemas in a separate `std::map<srl::SchemaHash, std::string>`.

## Problems

1. **`LevelDBTransaction::erase(variable_name)` is a silent no-op.** It seeks
   with `variable_name` and no keyspace byte, so the iterator lands outside the
   storage's keyspace and the `k[0] != kid` guard breaks the loop on the first
   iteration.

2. **`MemStorage::apply(OpReplicate)` uses moved-from values.** The key and
   value are moved into `_storage` and *then* read to build the outgoing
   `ReplicatorEvent`, so cascaded replication (A -> B -> C) forwards empty
   records.

3. **`MemStorage::apply(OpErase)` reports the last-revision pointer deletion
   with `erase = false`.** A replica stores an empty value instead of deleting
   the key.

4. **`ReplicatorEvent::key` is not portable.** It carries the raw physical key,
   including the LevelDB keyspace byte, and `IStorageTransaction::put(const
   ReplicatorEvent &)` writes it verbatim. Consequences: LevelDB keyspace A ->
   keyspace B writes into keyspace A of the target database; LevelDB ->
   MemStorage produces keys with a stray leading byte that `get()`/`list()`
   never match; MemStorage -> LevelDB writes into a keyspace chosen by the
   first character of the variable name. Schema records carry no marker at all,
   so `MemStorage` cannot route them to `_schemas` — the missing class-hash
   table replication.

5. **Three declared-but-undefined functions** (found by the test, which was the
   first binary ever to link the LevelDB backend): `LevelDBStorage::LevelDBStorage`,
   `LevelDBTransaction::LevelDBTransaction` and `LevelDBStorageManager::list()`.
   `quarkbot_impl` is a static library, so nothing complained until a test
   pulled the object file in.

6. **`get_enumerator` hands out a `ValueView` that aliases the leveldb
   iterator's own buffer, then advances it.** `leveldb::DBIter::value()` returns
   `iter_->value()` only while iterating forwards; after `Prev()` it returns a
   Slice into `DBIter::saved_value_`, a member string that every further `Prev()`
   overwrites. The enumerator filled `w.data` and immediately called
   `Prev()`/`Next()`, so the consumer read the value of the neighbouring record.
   Forward iteration is unsafe for the same reason as soon as `Next()` crosses
   an SST block boundary.

## Design

### ReplicatorEvent gains a Kind and drops the keyspace byte

```cpp
struct ReplicatorEvent {
    enum class Kind {
        ///data record: key is <variable_name> '\0' <RecordKey>, or
        ///<variable_name> alone for a last-revision pointer
        data,
        ///schema record: key is the binary srl::SchemaHash
        schema
    };
    std::string_view key;
    std::string_view value;
    bool erase;
    Kind kind = Kind::data;
};
```

`key` is the **logical** key — never contains a backend keyspace id. The inner
encoding above is the contract shared by all backends.

### LevelDB emission

`ReplicatorHandler` holds the owning storage's keyspace id. For each batch
record it inspects byte 0:

- `== schema_keyspace (0xFE)` -> emit `Kind::schema`, `key = physical.substr(1)`
  (the binary schema hash)
- `== _keyspace_id` -> emit `Kind::data`, `key = physical.substr(1)`
- anything else -> skip (a batch only ever touches its own keyspace plus the
  schema keyspace; a foreign byte means a bug and must not be replicated)

### LevelDB application

`LevelDBTransaction::put(const ReplicatorEvent &)` prefixes the key with
`schema_keyspace` for `Kind::schema` and with the transaction's own
`get_keyspace_id()` for `Kind::data`. This is what makes replication between
two logical keyspaces work.

### MemStorage emission and application

Emission is unchanged except for fixes 2 and 3; MemStorage keys are already
logical. `apply(OpPutSchema)` now also emits a `Kind::schema` event, so schemas
replicate out of MemStorage too.

`MemStorageTransaction::put(const ReplicatorEvent &)` records the kind, and
`apply(OpReplicate)` routes `Kind::schema` into `_schemas` (decoding the key
back to `srl::SchemaHash` via `std::bit_cast`) and `Kind::data` into
`_storage`. The event is re-emitted using the stored copies, not moved-from
ones.

### Lazy iterator advance

`LevelDBStorage::get_enumerator` advances the underlying iterator at the
*beginning* of the next call instead of right after filling `ValueView`. The
view then stays valid exactly as long as `ValueView` promises — until the next
iteration step — with no extra copy. Both directions carry an `advance` flag
that is false on the first call.

### Unchanged

`StorageNamespace` keeps forwarding `add_replicator` and `put(event)` to the
root storage: replication is defined at whole-storage granularity, not per
namespace. Namespace prefixes are part of the variable name and therefore
travel inside the logical key automatically.

### Known limitation (documented, not fixed)

- Replication is at-most-once. `commit()` writes the batch and then emits; a
  crash in between loses the events. Consumers that need completeness must
  catch up via `select_range`.
- Events carry no origin marker, so bidirectional replication between two
  storages echoes indefinitely. One-way replication is the supported topology.

## Test

New `src/tests/leveldb_storage_test.cpp`, registered in
`src/tests/CMakeLists.txt` only when `QUARKBOT_LEVELDB` is `ON` (that option is
off even under `QUARKBOT_TESTS`, unlike network/tardis/trth). The test opens a
database under a unique directory in the system temp path and removes it on
entry and exit.

Cases:

1. **manager** — distinct names get distinct keyspace ids; repeated
   `get_storage(name)` returns the same id; `list()` reports both names;
   `delete_storage()` removes the data and the directory entry, and a
   subsequent `get_storage()` yields an empty storage.
2. **put/get and last revision** — mirrors `mem_storage_test.cpp::test_put_get`,
   including that an uncommitted transaction has no effect.
3. **keyspace isolation** — the same variable name in two storages does not
   collide.
4. **erase** — `erase(variable)` removes every revision and the last-revision
   pointer; `erase(variable, key)` removes exactly one revision. Covers fix 1.
5. **list** — prefix filtering; schema records never appear as variables.
6. **select_range** — forward and reverse, mirroring
   `mem_storage_test.cpp::test_sequences`.
7. **UpdateLastRevision** — `enable` keeps history, `disable` leaves `get()`
   without a key unchanged, `enable_erase_last` drops the previous revision.
8. **schema binary** — round-trip, and visibility from a second storage in the
   same database (shared `0xFE` keyspace).
9. **persistence** — write, close the database, reopen, read back.
10. **replicator** — events arrive only after `commit()` and not for a dropped
    transaction; replaying the captured events reproduces the content in
    (a) a different keyspace of a second database and (b) a `MemStorage`,
    schemas included. Covers fix 4.

Fixes 2 and 3 are covered by extending `src/tests/mem_storage_test.cpp` with a
cascade case (MemStorage -> MemStorage -> MemStorage) and an erase-replication
case.
