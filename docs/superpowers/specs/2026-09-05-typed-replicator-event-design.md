# Typed ReplicatorEvent: logical fields instead of a binary key

Date: 2026-09-05

## Background

`IStorage::ReplicatorEvent` (include/quarkbot/abstract/istorage.hpp) currently
describes a record change as an opaque byte string plus two flags:

```cpp
struct ReplicatorEvent {
    std::string_view key;
    std::string_view value;
    bool erase;
    bool schema_hash;
};
```

The `key` is *logical* in the sense that it carries no backend keyspace prefix,
but it still carries the physical key *layout* that both current backends share:

```
<variable_name> '\0' <big-endian uint64 ordered> <big-endian uint64 random>   data record
<variable_name>                                                              last-revision pointer
<big-endian SchemaHash>                                                      schema record (schema_hash == true)
```

Introduced by the 2026-07-31 storage-replication design, this shape works for
replicating one key-value store into another. It does not work for replicating
into a store that is not key-value.

## Problem

A relational replication target stores `name` and `recordkey` in separate
columns and the schema hash as a number. With the current event it has to parse
the binary key: strip a 16-byte big-endian suffix, test the `'\0'` separator to
tell a data record from a last-revision pointer, and `bit_cast` 8 bytes to get a
`SchemaHash`. That parser is a copy of the backend's private key format living
in the consumer, and the API forces every consumer to own one.

The consumers in this repository already show what that costs:

- `json_report.cpp:159-165` reimplements the layout test
  (`remove_suffix(16)`, `s.back() == 0`) before it can report a variable update.
- `var_inspector.cpp:26` inserts `std::string(ev.key)` for every non-schema
  event, so binary keys accumulate in `_updated_vars`. It works only because
  `inspect_all_updated` silently drops names that fail to resolve, and because
  the last-revision pointer event happens to carry a clean name. A put with
  `UpdateLastRevision::disable` emits no pointer event and is therefore never
  reported.
- `persistent_reporter.cpp:56` prints `ev.key` — a name followed by 17 raw
  bytes — into a text log, and additionally renders every pointer event as a hex
  blob, because `extract_srl` on a 16-byte value yields a garbage schema hash.
- `simple_stdio_debugger.cpp:42` looks a watchpoint up by `ev.key`, which never
  matches a watch name for a data record. Watchpoints fire only via the pointer
  event, so with `UpdateLastRevision::disable` they never fire at all.

The LevelDB backend, by contrast, owns its key format outright: it builds
physical keys in `build_key()` and already decodes them when iterating the
committed `WriteBatch`. Decoding there rather than in every consumer puts the
knowledge where it belongs.

## Approach

Replace the binary key with the logical fields it encodes, and add an event type
that says which fields carry meaning. No union and no variant: the alternatives
share most of their fields and the `string_view` members borrow foreign buffers,
so `std::visit` would add ceremony without buying safety.

Each backend decodes its own key format when emitting events and encodes it back
when applying them. An event stays applicable to a different keyspace, a
different backend, or a relational table keyed by `(name, recordkey)`.

## Design

### 1. The event structure

```cpp
///Describes single record change performed by a committed transaction
/**
    The event is *logical*: it never carries a backend specific keyspace or
    instance prefix, nor the binary key layout a backend happens to use. A
    backend decodes its own key format when emitting events and encodes it back
    when applying them, so an event stays applicable to a different keyspace, a
    different backend, or a store that is not key-value at all - a relational
    table keyed by (name, recordkey).
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
};
```

Field validity per type:

| `type` | `name` | `recordkey` | `value` | `schema_hash` |
|---|---|---|---|---|
| `put_schema` | - | - | yes | yes |
| `put_key_value` | yes | yes | yes | - |
| `update_latest` | yes | yes | - | - |
| `erase_key` | yes | yes | - | - |
| `erase_latest` | yes | - | - | - |
| `erase_name` | yes | - | - | - |

Fields other than `type` get default member initializers, so an event is written
with a designated initializer naming exactly the fields that matter:
`ReplicatorEvent{.type = Type::erase_name, .name = "alpha"}`. No static factory
functions are added; a named-field aggregate is equally readable and adds no API.

`Type` is nested in `ReplicatorEvent` so it does not leak into the `quarkbot`
namespace. A consumer switching on it introduces a local alias
(`using Type = Storage::ReplicatorEvent::Type;`) rather than spelling the
qualified name in every `case`.

`erase_latest` and `erase_name` both exist on purpose. `erase_name` is the
logical operation matching `IStorageTransaction::erase(variable_name)`, which a
relational backend serves with one `DELETE ... WHERE name = $1`. `erase_latest`
is the physical disappearance of the pointer record, which LevelDB reads out of
its committed batch. Two different statements of intent that reach the same
state.

`Replicator` changes from `SignalSlot<void(ReplicatorEvent)>` to
`SignalSlot<void(const ReplicatorEvent &)>`. The struct grows from 40 to 64
bytes, and every consumer lambda already takes a const reference —
`Storage::add_replicator` requires it in its `requires` clause
(include/quarkbot/storage.hpp:106). Two test lambdas take the event by value and
need updating.

The `string_view` members keep borrowing: they point into the committed
`WriteBatch` or a LevelDB slice and are valid for the duration of the callback.
A handler that needs to retain an event copies it, as `CapturedEvent` in
leveldb_storage_test.cpp already does.

### 2. Emitting events

#### LevelDB: decoding the batch

`LevelDBTransaction::commit()` is unchanged — it writes the batch, then iterates
it. Only `LevelDBStorage::ReplicatorHandler::emit()` changes, from stripping the
keyspace byte to full decoding:

| physical key | `Put` | `Delete` |
|---|---|---|
| keyspace == `schema_keyspace` | `put_schema{schema_hash, value}` | dropped |
| has `'\0'` + 16-byte suffix | `put_key_value{name, recordkey, value}` | `erase_key{name, recordkey}` |
| otherwise (pointer) | `update_latest{name, recordkey}` | `erase_latest{name}` |

Three details:

**The `recordkey` of `update_latest` comes from the record's *value*, not its
key.** A pointer record keys on the name alone and stores the 16-byte revision as
its value. A value whose length is not `recordkey_string_size` yields no event —
the same guard `LevelDBStorage::get` already applies
(src/quarkbot/leveldb/leveldb_storage.cpp:170).

**A delete in the schema keyspace is dropped rather than emitted.** No
`IStorageTransaction` operation erases a schema, and `delete_storage` clears only
its own keyspace and directory entry, so the case is unreachable. The enum has no
type for it and none is invented for an unreachable path.

**Telling a data key from a pointer key stays a heuristic.** A variable name
whose last 17 bytes are `'\0'` followed by 16 arbitrary bytes reads as a data
record. This is inherent to the physical layout, `json_report.cpp:159` already
makes the same test, and variable names are identifiers in practice. Accepted as
a documented constraint.

`ReplicatorHandler::keyspace_id`
(src/quarkbot/leveldb/leveldb_storage.hpp:103) is never read — `emit` takes the
keyspace from the key's first byte — and is removed.

#### MemStorage: emitting from intent

`MemStorage` mirrors the LevelDB inner key layout, but its `apply()` overloads
know the intent and decode nothing:

| operation | emitted events |
|---|---|
| `OpPut`, `mode == disable` | `put_key_value` |
| `OpPut`, `mode == enable` | `put_key_value`, `update_latest` |
| `OpPut`, `mode == enable_erase_last` | `put_key_value`, `erase_key` (previous revision), `update_latest` |
| `OpEraseRev` | `erase_key` |
| `OpErase` | `erase_name` — one event |
| `OpPutSchema` | `put_schema` |
| `OpReplicate` | re-emits the applied type from the stored copies |

Two behaviour changes:

**`OpErase` emits a single `erase_name`** instead of today's N per-record erases
(src/quarkbot/common/mem_storage.hpp:212-224). This is the intended asymmetry: a
MemStorage source says "erase the variable", a LevelDB source says "erase these
records and the pointer". Both reach the same state at different granularity.

**`OpPut` event order aligns with LevelDB** as `put_key_value` → `erase_key` →
`update_latest`. MemStorage currently emits the pointer before the data
(mem_storage.hpp:238 vs 242), which briefly points a replica at a record that
does not exist yet. Nothing depends on that ordering (see "Transaction
boundaries" below), but one expected sequence for both backends makes the events
testable against a single expectation.

`OpReplicate` is widened to a materialized event — `type`, `std::string name`,
`RecordKey recordkey`, `std::string value`, `srl::SchemaHash schema_hash` — so
that an A -> B -> C cascade keeps forwarding real content, which
`test_replication_cascade` guards.

### 3. Applying events

`IStorageTransaction::put(const ReplicatorEvent &)` is renamed to
`apply(const ReplicatorEvent &)`. It is currently the third `put` overload and
the only one that can also delete; `put` with `type == erase_name` reads as the
opposite of what it does. Eight call sites change: the declaration in
`istorage.hpp`, both backend implementations, `StorageNamespaceTransaction`, the
`StorageTransaction` wrapper (include/quarkbot/storage.hpp:170),
`replicate_from_message`, and two test helpers (`forward` in
mem_storage_test.cpp:267, `replay_into` in leveldb_storage_test.cpp:467).

`LevelDBTransaction::apply`:

| `type` | applied as |
|---|---|
| `put_schema` | `put_schema_binary(ev.schema_hash, ev.value)` |
| `put_key_value` | `_batch.Put(build_key(kid, ev.name, ev.recordkey), ev.value)` |
| `update_latest` | `_batch.Put(build_key(kid, ev.name), record_key_to_string(ev.recordkey))` |
| `erase_key` | `erase(ev.name, ev.recordkey)` |
| `erase_latest` | `_batch.Delete(build_key(kid, ev.name))` |
| `erase_name` | `erase(ev.name)` — scan and decompose |

`put_key_value` writes **straight into the batch, not through
`put(name, key, value, enable)`**. That method would also write the pointer
record, which arrives as its own `update_latest` event; routing through it would
materialize a pointer in the replica for a source that wrote with
`UpdateLastRevision::disable`.

`erase_name` needs no new code: `LevelDBTransaction::erase(variable_name)`
(leveldb_storage.cpp:312) already scans the keyspace and puts one `Delete` per
record including the pointer, so decomposition is symmetric to emission by
construction.

Two documented consequences:

- **An `erase_name` applied to LevelDB re-emits as `erase_key` × N plus
  `erase_latest`** on its own commit. A MemStorage -> LevelDB -> X cascade
  refines granularity. Consistent with the asymmetry above.
- **`erase(ev.name)` scans the database, not the batch**, so records written
  earlier in the same uncommitted transaction are not erased. This is an existing
  property of that method, but applying a replication stream hits it more often.

`MemStorage` applies through the widened `OpReplicate` at commit time,
symmetrically: `wholeKey(name, recordkey)` for `put_key_value` and `erase_key`,
the bare `name` for `update_latest` and `erase_latest`, the range
`[name + '\0', name + '\x01')` plus the pointer for `erase_name`, and
`_schemas[hash]` for `put_schema`. It re-emits from the stored copies so the
cascade holds.

#### Namespace prefixing fix

`StorageNamespaceTransaction::put(event)`
(include/quarkbot/storage_namespace.hpp:89-91) forwards the event to the root
storage **without `add_prefix`**, while every other method prefixes. Replicating
into a namespaced storage therefore writes outside the namespace, contradicting
the documented promise that an event can be applied "to a different logical
keyspace of the same backend". With a binary key this could not be fixed without
parsing it; with a typed `name` it is one line.

`apply()` prefixes `ev.name` for every type except `put_schema`, which has no
name and whose schemas are global.

`StorageNamespace::add_replicator` (storage_namespace.hpp:41) also just delegates
to the root, so a replicator attached to a namespace sees every root event with
fully prefixed names. That is the mirror-image question and is deliberately out
of scope here.

### 4. Message bus wire format

`replicate_from_message` (src/quarkbot/common/storage_msgbus_replicator.cpp:88-90)
contains:

```cpp
if (ch1 == 'E')  erase = true;
else if (ch2 == 'P') erase = false;   // ch2, should be ch1
else return false;
```

The writer emits `<'P'|'E'><'S'|'R'>`, so a row put is `"PR"`: `ch1` is `'P'` and
`ch2` is `'R'`. The first test fails, the second tests `ch2 == 'P'` against
`'R'` and also fails, and the message is dropped. Only messages starting with
`'E'` are accepted, so **put replication over the message bus has never
worked**, and there is no test covering it. The new format removes the
two-character construction entirely, fixing this as a side effect.

New format — one type byte, then only the fields that type carries:

```
<T><payload...>

T = 'S' put_schema     <8B schema hash, big endian> <blob value>
    'P' put_key_value  <blob name> <16B recordkey> <blob value>
    'L' update_latest  <blob name> <16B recordkey>
    'K' erase_key      <blob name> <16B recordkey>
    'R' erase_latest   <blob name>
    'N' erase_name     <blob name>

<blob>      = <varlen size><bytes>, size big endian with continuation bit 7
<recordkey> = 16 bytes big endian, the same encoding as a physical key
```

- The `recordkey` is encoded with `record_key_to_string` /
  `string_to_record_key` from `storage_common.hpp` — the same encoding physical
  keys use, so no new encoder is needed and `apply()` on LevelDB can compose the
  key from those bytes directly.
- The existing `write_blob` / `extract_blob` and varint helpers stay; only
  message composition and parsing change. Buffer reservation becomes
  `1 + 16 + 2 * varint_max + name.size() + value.size()`, i.e. 37 bytes of
  overhead. The "512 bytes on the stack, heap otherwise" pattern
  (storage_msgbus_replicator.cpp:52-61) stays.
- **The parser validates fixed-width fields too.** `extract_blob` clamps a blob
  length to the remaining message, but reading a 16-byte `recordkey` or an 8-byte
  hash must check the remaining length itself and return `false` when short.
  Otherwise a truncated message yields a `RecordKey` built from arbitrary bytes,
  which is worse than a dropped message.
- The content type moves from `application/prs.db-repl.command` to
  `application/prs.db-repl.command.v2`. The format is incompatible and
  `replicate_events` filters on the content type
  (storage_msgbus_replicator.cpp:110), so an old message reaching a new reader
  fails the filter rather than parsing into nonsense. Compatibility is academic
  anyway, given that puts never got through.

Encoding moves out of the `static replicate_with_buffer` into the header as a
pair that leaves allocation to the caller, preserving today's allocation-free
path and making the format testable without a message bus or an
`ExecutionWorker`:

```cpp
///Number of bytes encode_replication_message needs for this event
std::size_t replication_message_size(const Storage::ReplicatorEvent &ev);

///Encodes event into the wire format; returns the written prefix of buffer
std::span<char> encode_replication_message(const Storage::ReplicatorEvent &ev, std::span<char> buffer);
```

`attach_replicator` becomes a thin wrapper over that pair plus `bus.send()`.

### 5. Consumers

Each consumer becomes a `switch`, and three of them lose behaviour that reads as
unintended (see "Problem" above for what each does today).

- **`json_report.cpp:149`** — `put_schema` uses `ev.schema_hash` directly;
  `put_key_value` fills `{"name", ev.name}` and
  `{"rev", {ev.recordkey.ordered, ev.recordkey.random}}`. The
  `remove_suffix(16)` / `s.back() == 0` block (lines 159-165) goes away. Observable
  behaviour is unchanged: today's `!ev.erase` filter plus the 17-byte suffix test
  admit exactly the records that `case put_key_value` admits.
- **`var_inspector.cpp:14`** — `put_schema` fills
  `_schema_cache[ev.schema_hash]`; `put_key_value` does
  `_updated_vars.insert(std::string(ev.name))`. This stops the set from
  accumulating binary keys and makes a `UpdateLastRevision::disable` put report
  its variable.
- **`persistent_reporter.cpp:38`** — `put_schema` does
  `schema_cache.emplace(ev.schema_hash, ...)`; `put_key_value` prints `ev.name`.
  Restricting to `put_key_value` removes both the binary name and the hex line
  per pointer update.
- **`simple_stdio_debugger.cpp:40`** — `_watches.find(ev.name)` on
  `put_key_value`. A watchpoint now fires exactly once per record write,
  including under `UpdateLastRevision::disable`.

All four let the remaining types fall through: `update_latest`, `erase_latest`
and `erase_key` add nothing for reporting, and deletions are not surfaced
anywhere today.

### Dead code removed

- `schema_hash_to_key` and `schema_key_to_hash`
  (src/quarkbot/common/mem_storage.hpp:246-255) exist only because the schema
  hash travelled as a binary string. All three call sites
  (`var_inspector.cpp:16`, `json_report.cpp:151`,
  `persistent_reporter.cpp:41`) and MemStorage's own use disappear.
- `LevelDBStorage::ReplicatorHandler::keyspace_id`.

### Transaction boundaries: deliberately not addressed

The replicator emits one event per record, not per transaction, and this design
keeps it that way. A transaction here is a batch, not an atomicity guarantee:
even a relational target cannot promise write visibility under master-slave
replication or lazy commits, so a `commit` event or a batched replicator
signature would encode a guarantee that does not exist. `replicate_events`
continues to accumulate into `shared_transaction(storage, CommitMode::lazy)`.

### Value format: deliberately not exposed

A stored value is `<serialized data><8-byte schema hash>`, but that convention
lives in `storage_srl.hpp` above `IStorage`, for which a value is an opaque byte
range. `put_key_value` therefore does not carry the value's schema hash;
`schema_hash` is meaningful only for `put_schema`. A consumer that wants the hash
of a data record calls `extract_srl`, as `persistent_reporter.cpp:44` and
`json_report.cpp:167` already do. Extraction failure is not a correctness
concern: it only means the schema is not found and the value renders as a hex
blob.

## Testing

TDD throughout: a test for each behaviour first, then the implementation. The
harness is the macro set in `src/tests/check.h`.

### `mem_storage_test.cpp` (always built)

`test_replication_cascade` stays as is — the A -> B -> C cascade over put,
schema and `erase("var")` must pass with unchanged semantics, only with `apply()`
renamed in the `forward()` helper.

`test_erase_replicates_as_erase` is rewritten as `test_erase_emits_erase_name`:
exactly one event, `type == erase_name`, `name == "var"`. Today's version expects
two events with `erase` set, so it must change — that is the intended
granularity change.

New:

- **`test_put_event_sequence`** — the three `UpdateLastRevision` values against
  their expected sequences: `disable` gives `put_key_value` alone; `enable` gives
  `put_key_value`, `update_latest`; `enable_erase_last` gives `put_key_value`,
  `erase_key` (previous revision), `update_latest`, in that order. Covers the
  unified emission order and asserts that `erase_key` carries the *old* revision.
- **`test_schema_event_is_numeric`** —
  `put_schema_binary(SchemaHash{0x1234}, "blob")` gives one `put_schema` with
  `schema_hash == 0x1234` and `value == "blob"`.
- **`test_erase_single_revision_event`** — `erase("v", {1,0})` gives one
  `erase_key` with the right `recordkey`.
- **`test_apply_into_namespace`** — regression test for the prefixing fix. A root
  `MemStorage`, a `"ns/"` namespace, and a `put_key_value{.name = "alpha", ...}`
  applied through the namespace transaction. Expect `namespace->get("alpha", key)`
  to find the value, `root->get("ns/alpha", key)` to find it too, and
  `root->get("alpha", key)` **not** to. Same for `erase_name`, which must only
  remove records under the prefix. This test fails against today's code.

### `leveldb_storage_test.cpp` (only with `QUARKBOT_LEVELDB=ON`)

`CapturedEvent` and `EventLog` (lines 445-471) move to the new structure —
`type`, `recordkey` and `schema_hash` by copy, `name` and `value` into
`std::string`.

`test_replicator_fires_on_commit` asserts concrete types instead of
`for (...) CHECK(!ev.erase)`.

`test_replicated_key_is_logical` becomes `test_replicated_event_is_typed`: the
byte-layout assertions (`ev.key.compare(0, 5, "alpha")`,
`ev.key.size() == 5 + 1 + 2*sizeof(std::uint64_t)`) are replaced by
`name == "alpha"`, `recordkey == RecordKey{1,0}` and
`schema_hash == 0xABCD`. It is rewritten not because its expectation becomes
wrong, but because it asserts a key layout that no longer exists.

`test_replication_to_other_keyspace` and `test_replication_to_mem_storage` stay,
routed through `apply()`.

New:

- **`test_erase_variable_decomposes`** — `erase("alpha")` over a variable with
  two revisions gives `erase_key` × 2 with the right `recordkey`s plus one
  `erase_latest`, and **no** `erase_name`.
- **`test_apply_erase_name_decomposes`** — a hand-built `erase_name` applied to a
  LevelDB storage holding records: after commit (a) the records and the pointer
  are gone, (b) the re-emitted stream is `erase_key` × N plus `erase_latest`.
  Covers decomposition symmetry in both directions.
- **`test_apply_pointer_and_data_independently`** — an `update_latest` alone
  moves `get(name)` without writing data; a `put_key_value` alone makes
  `get(name, key)` available while leaving `get(name)` untouched. Guards the
  decision to write `put_key_value` straight into the batch.

### New wire-format test

There is no round-trip test for the message bus today, which is how the
`ch2`/`ch1` typo survived. Add `storage_replication_msgbus.cpp` to `BASIC_TESTS`
in src/tests/CMakeLists.txt. `quarkbot_backtest` links `quarkbot_impl` publicly
and `storage_msgbus_replicator.cpp` belongs to `impl`, so a `BASIC_TESTS` binary
reaches it transitively.

For each of the six types: encode, feed to `replicate_from_message` against a
`MemStorage` transaction, commit, and compare the *re-emitted* event with the
original. The decoded event validates itself through the storage, without
exposing the parser separately.

Negative cases: a message truncated mid-`recordkey`, truncated mid-hash, empty,
or carrying an unknown type byte must return `false` and write nothing into the
transaction.

### Verification

```bash
cmake -DCMAKE_CXX_COMPILER=g++-14 -DQUARKBOT_LEVELDB=ON ..
cmake --build . -j$(nproc)
ctest --test-dir build
```

`QUARKBOT_LEVELDB=ON` must be passed explicitly: `QUARKBOT_TESTS` does not enable
it (src/tests/CMakeLists.txt:75), so without it neither the LevelDB backend
changes nor its test would compile.
