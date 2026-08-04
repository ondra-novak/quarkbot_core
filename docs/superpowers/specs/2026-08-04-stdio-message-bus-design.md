# Stdio message bus: JSON Lines transport for `IMessageBus`

Date: 2026-08-04

## Background

`IMessageBus` (`include/quarkbot/abstract/imessage_bus.hpp`) defines the
strategy's communication hub: `subscribe()` returns an `IEventStream<Message>`,
`send()` posts a `Message` out. The only implementation today is
`IMessageBus::Null`, which logs and discards.

`Message` carries `type`, `sender`, `target`, `payload`, `conversation_id`,
`schema` (a `SchemaHash` = `uint64_t`), `send_time` and an `ownership`
`RefCountPtr` whose job is to keep the buffer backing the `string_view` fields
alive.

The first in-tree producer is the storage replicator
(`src/quarkbot/common/storage_msgbus_replicator.cpp`), which emits binary frames
(`'P'`/`'E'`, `'R'`/`'S'`, varint-prefixed key and value). That is one use case
among many — the bus is a general hub, and the expected peer is a router or
orchestrator process, quite possibly written in JavaScript.

This spec covers a transport that connects the bus to a pair of file
descriptors, normally stdin/stdout.

## Goals and non-goals

**Goals**

- One process-level component: read loop, MT-safe subscriber registration,
  synchronous send, EOF propagation.
- Wire format defined by a policy class supplied as a template parameter.
- A JSON Lines format that a JavaScript peer can drive with `readline` +
  `JSON.parse`, and that survives Windows stdio.
- End-to-end backpressure toward the peer.

**Non-goals**

- Routing, groups, service discovery, announce lifecycle. The `MessageType`
  values are transported faithfully; acting on them is the router's job.
- Integration with a non-blocking event loop. The read loop is blocking by
  design (see *Read loop*).
- Reconnection. The transport lives and dies with its file descriptors.

## Why JSON Lines, and why it is the only format for now

The alternative considered was an LSP-style framing — human-readable headers
plus `Content-Length` and a raw binary body. It has zero payload encoding
overhead, which matters for a high-rate C++-to-C++ link.

It loses on the two constraints that actually apply here:

1. **Windows stdio.** Text mode translates `\n` to `\r\n`. Any format with a
   length-counted raw body breaks when the mode is not set correctly on both
   ends, and that failure mode has already been observed in practice. JSON Lines
   has no length-counted bytes at all: `Json::write_string`
   (`include/quarkbot/json/json.hpp:437-459`) escapes every control character
   including `\n` and `\r`, so a serialized `Json` is always exactly one line. A
   stray `\r` on read is stripped; a stray `\r` on write is harmless.

2. **A JavaScript peer.** `JSON.parse` is free; a varint/`Content-Length`
   parser is not.

So `HeaderMessageFormat` is **not implemented**. The `Format` template parameter
stays so a binary format can be added when a peer exists that needs it, but
writing it now would be unused code. Consequence: `MessagePipe` exposes
`read_line()` only; `read_exact()` arrives with the format that needs it.

## Wire format

One message per line, UTF-8, `\n`-terminated. A single JSON object:

```json
{"type":"normal","from":"","to":"storage-replica","conv":"12345","schema":"9f2c1a3b7d004e11","time":1784700083313,"enc":true,"payload":"UEsDBBQA"}
```

| field | JSON type | maps to | omitted on encode when | missing on decode means |
| --- | --- | --- | --- | --- |
| `type` | string | `MessageType` | never | `normal` |
| `from` | string | `sender` | empty | empty |
| `to` | string | `target` | empty | empty |
| `conv` | string, decimal | `conversation_id` | `0` | `0` |
| `schema` | string, 16 hex digits | `schema` | `0` | `0` |
| `time` | number, unix ms | `send_time` | never | receive time |
| `enc` | bool | payload encoding | `false` | `false` |
| `payload` | string | `payload` | empty | empty |

`type` uses the enum names: `normal`, `announce`, `add_to_group`,
`group_close`, `no_route`. Self-documenting for a script peer and extensible
without renumbering.

Every field has a default, so `{}` is a valid (empty, `normal`) message. That is
deliberate: a router developer hand-feeding lines into the process should not
have to supply a timestamp to get a message through.

`conv` and `schema` are strings because they use the full `uint64_t` range and
JSON numbers lose precision above 2^53. `time` is a number: unix milliseconds is
~1.78e12 today, so 2^53 is not a real ceiling, and `new Date(msg.time)` is
nicer than `new Date(Number(msg.time))`.

**Payload encoding.** `enc` absent or `false` means `payload` is text and its
bytes are the payload verbatim. `enc: true` means `payload` is standard base64
(with `=` padding) of an arbitrary byte string. The encoder picks text when the
payload is valid UTF-8 and base64 otherwise, so a strategy sending text messages
pays no encoding tax and the line stays readable; the replicator's binary frames
go base64.

**Compatibility rules.**

- Unknown object members are ignored on parse.
- An unknown `type` value logs a warning and drops the message.
- A line that is not parseable JSON, or is JSON but not an object, logs a
  warning and is skipped — the read loop continues. A router that prints a
  stray diagnostic line to its stdout must not kill the strategy.
- A line longer than `Config::max_line` (default 16 MiB) is a protocol
  violation: log an error and terminate the read loop.
- A truncated final line at EOF is EOF, not an error.

## Components

```
StreamMessageBus<Format>            header, template
  ├── MessagePublisher              subscriber registry + per-subscriber queues
  ├── MessagePipe                   .cpp — fds, input buffer, write mutex, platform bits
  └── Format                        JsonMessageFormat (only implementation)
```

Files, all under `src/quarkbot/common/`:

- `message_pipe.hpp` / `message_pipe.cpp`
- `json_message_format.hpp` / `json_message_format.cpp`
- `stream_message_bus.hpp` (template, includes the publisher)

with `using JsonMessageBus = StreamMessageBus<JsonMessageFormat>;` as the
convenience alias.

### `MessagePipe`

Owns the two descriptors and all platform-specific behaviour.

```cpp
class MessagePipe {
public:
    static constexpr std::size_t default_max_line = 16u*1024*1024;

    static MessagePipe stdio(std::size_t max_line = default_max_line);
    MessagePipe(int fd_in, int fd_out, std::size_t max_line = default_max_line,
                bool own_fds = false);

    // reader side — single-threaded, no lock
    std::optional<std::string_view> read_line(); // nullopt = EOF or over-long line
    bool line_too_long() const;                  // distinguishes the two

    // writer side — MT safe
    bool write(std::string_view data);            // atomic w.r.t. other writers
    bool broken() const;
};
```

`read_line()` accumulates into an internal `std::string`, returns a view of
`[start, '\n')` with a trailing `\r` stripped, and compacts the buffer once the
consumed prefix exceeds half its size. The returned view is valid until the next
`read_line()`.

Raw `::read`/`::write` (`_read`/`_write` on Windows), not `FILE*` — no stdio
buffering to coordinate with, and no flush step to forget. Both loop over
partial transfers and retry `EINTR`. A write that cannot complete sets
`broken()`.

`write()` holds a mutex across the whole message, so concurrent senders never
interleave a line.

`stdio()` calls `_setmode(_fileno(stdin), _O_BINARY)` and the stdout equivalent
on Windows. The format is newline-safe regardless, but binary mode keeps written
bytes exact; the reader tolerates `\r` anyway for peers that do not set it.

### `Format` concept

Pull-based: the format reads what it needs from the pipe. With a blocking read
loop there is no reason for an incremental state machine, and a straight-line
parser is much easier to get right.

```cpp
enum class DecodeResult { ok, skipped, eof };

struct JsonMessageFormat {
    static void encode(const Message &msg, std::string &out);   // appends one line incl. '\n'
    static DecodeResult decode(MessagePipe &in, Message &msg);
};
```

`skipped` is a malformed-but-recoverable line: the loop logs and continues.

`decode` sets `msg.ownership` itself — no separate out-parameter for the buffer.
It holds a `RefCountPtr<MessageBuffer>` locally, points the `string_view` fields
at it, then assigns it to `ownership`, where the converting constructor
(`refcnt.hpp:40-43`) narrows it to the base type.

### `MessageBuffer`

Backing store for one decoded message, refcounted so subscribers can copy the
`Message` freely.

```cpp
class MessageBuffer: public RefCountInstanceWithDeleter {
public:
    static RefCountPtr<MessageBuffer> create(std::string sender, std::string target,
                                             std::string payload);
    std::string_view sender() const;
    std::string_view target() const;
    std::string_view payload() const;
};
```

Three separate `std::string` members rather than one appended blob: appending to
a single string can reallocate and dangle the views already handed out, and SSO
covers the short fields anyway. `Message::ownership` holds the `Ptr`, so the
buffer outlives every queued copy.

### `MessagePublisher`

Internal to `StreamMessageBus`. **One mutex guards everything** — subscriber
list, all queues, all awaiter slots. Messages arrive at pipe rate, not market
data rate, so contention is not a concern, and a single lock removes the lock
inversion that the existing `QueueEventPublisher` has by construction.

```cpp
class MessagePublisher {
public:
    std::shared_ptr<IEventStream<Message> > subscribe();
    void publish(const Message &msg);   // blocks while any queue is at max_queue
    void close();                       // EOF: every awaiter completes false
};
```

The list holds `std::weak_ptr<MessageQueueStream>`; `publish()` and `close()`
upgrade to `shared_ptr` under the lock, which guarantees the stream survives the
resume that happens after the lock is released.

`MessageQueueStream` derives from `EventStreamStoppable<Message>` and owns no
mutex of its own. Per subscriber: a `std::deque<Message>`, a pending
`awaitable<bool>::result`, the `Message *` it should be written into, and the
`PExecutionWorker` captured from `IExecutionWorker::current()` at suspend time.

**Worker transfer is mandatory.** `~prepared_coro()` resumes inline
(`prepared_coro.hpp:41-43`), so discarding the value returned by a promise runs
the strategy coroutine on the *reader* thread. `publish()` therefore collects
`(worker, handle)` pairs under the lock and, after unlocking, calls
`worker->resume(h)` — the pattern in `ExecutionWorker::schedule()`
(`execution_worker.hpp:106-110`) — falling back to `h.resume()` only when no
worker was recorded.

**Backpressure.** After delivering, `publish()` waits on a `condition_variable`
while any subscriber queue holds `Config::max_queue` messages (default 1000),
logging a warning on the first block. Because the read loop stops reading, the
pipe buffer fills and the peer feels real end-to-end backpressure. The accepted
cost: a strategy that abandons its stream without closing it stalls the read
loop, which the orchestrator sees as a wedged instance — a detectable, killable
state, unlike silent memory growth.

`close()` sets the closed flag, drains all pending awaiters to `false`, and
notifies the drain condition variable.

### `StreamMessageBus<Format>`

```cpp
template<typename Format>
class StreamMessageBus: public IMessageBus {
public:
    struct Config {
        std::size_t max_queue = 1000;
    };

    StreamMessageBus(MessagePipe pipe, Config cfg = {});

    std::shared_ptr<IEventStream<Message> > subscribe() override;
    void send(const Message &msg) override;      // MT safe

    void run();                                   // blocking; returns at EOF
    void request_stop();                          // takes effect between messages
};
```

`send()` encodes into a `thread_local std::string` (cleared, not reallocated)
and hands it to `MessagePipe::write()`. A broken pipe logs an error once, marks
the bus broken so later sends are no-ops, and requests stop so `run()` returns.

`run()` loops `Format::decode`, dispatching `ok` to `publish()`, `skipped` to a
warning, `eof` to the exit. On exit it calls `publisher.close()`, so every
strategy blocked in `co_await stream.next(...)` wakes with `false`. It is called
once per bus and is not reentrant.

`request_stop()` also notifies the publisher's drain condition variable and makes
a blocked `publish()` return early — otherwise a stop requested while the read
loop sits in backpressure would never be observed.

## Read loop and shutdown

`run()` is called from `main` after the strategy and its worker are up. The main
thread has no execution worker and nothing else to do, so a blocking read there
is the natural design; no reader thread is created.

Shutdown is **the peer closing our stdin**. `request_stop()` exists but only
takes effect between messages, because interrupting a blocking read is not
portable (POSIX needs a self-pipe plus `poll`, Windows needs
`CancelSynchronousIo`). This limit is documented rather than engineered around:
managing instance lifetime is the router's job, and the router closes the pipe.

## Prerequisite fixes

These are on the path and must land with the work:

1. **Move base64 into the SDK.** `src/quarkbot/network/base64.hpp` sits behind
   `QUARKBOT_NETWORK` and declares `base64_t` in the *global* namespace. Move it
   to `include/quarkbot/utils/base64.hpp`, wrap it in `namespace quarkbot`, and
   update `src/quarkbot/network/ws.cpp:4` (its two call sites at lines 42 and 50
   are already inside the namespace, so unqualified lookup keeps working).

2. **Fix `MessageBus::send<T>`** (`include/quarkbot/message_bus_srl.hpp:25-29`).
   It never compiled because it was never instantiated: it forwards
   `std::move(payload)` (the `const T &`) instead of the serialized bytes `s`,
   and calls `srl::schema_hash<T>()` although `schema_hash` is a variable
   template (`serialize.hpp:910`). This bus is its first real user.

3. **Drop the pointless copy in `MessageBus::send_raw`**
   (`message_bus.hpp:39`): it builds a `std::string(target)` temporary to
   initialise a `std::string_view` field. Valid for the duration of the call,
   but an allocation for nothing — pass `target` through.

## Out of scope, worth a separate ticket

`QueueEventPublisher` / `QueueEventStream`
(`src/quarkbot/streaming/queue_event_stream.hpp`) is dead code that has never
run. `simtradableinstrument.cpp:51-54` creates one for `ExternalFill` but never
publishes, so `publish()` was never instantiated — and it does not compile:
line 126 is `out.emplace_back(out, sub->push(val))` and `prepared_coro` has no
such constructor. Beyond that: `_closed` (line 101) is uninitialised; the
publisher has no `close()` and `QueueEventStream::close()` never resolves a
pending awaiter, so there is no EOF path; and `publish()` takes the publisher
lock then the subscriber lock while `close()` takes them in the opposite order.

This bus deliberately does not reuse it. The requirements differ (worker
transfer, EOF broadcast, refcounted payloads) and repairing code the simulator
links would widen the blast radius for no gain here.

## Testing

New test source `src/tests/message_bus_stream.cpp`, added to `BASIC_TESTS` in
`src/tests/CMakeLists.txt`.

**Format round-trip** — for each case, `encode` then `decode` and compare all
`Message` fields:

- text payload (ASCII and multi-byte UTF-8) stays `enc`-free
- binary payload containing `\0`, `\r`, `\n` and invalid UTF-8 goes base64
- empty `sender`, empty `target`, empty payload
- `conversation_id` and `schema` at `UINT64_MAX`
- every `MessageType` value
- payload whose byte length is 1, 2 and 3 mod 3 (base64 padding)

**Malformed input** — each must be `skipped`, not fatal, and the following
valid line must still decode:

- not JSON; JSON but an array or a scalar; unknown `type`; `enc: true` with
  invalid base64

**Accepted, not skipped** — `{}`; a message with unknown extra members; a
message with no `time` (which gets the receive time); a message with no `type`
(which becomes `normal`).

**Over-long line** — a line above `MessagePipe`'s `max_line` terminates the loop
with an error and does not allocate unboundedly.

**Pipe level** — `read_line()` fed in fragments across `read()` boundaries,
including a `\n` arriving alone; CRLF terminators; a final line without `\n`
followed by EOF; a 1 MiB line.

**End to end** over two `pipe(2)` pairs, one bus per side, strategy coroutines
on a real worker:

- a message sent on side A arrives on side B's subscriber and resumes on the
  subscriber's worker, not the reader thread
- two subscribers on one bus both receive every message
- a subscriber that closes stops receiving and does not block the other
- closing side A's write end makes side B's `run()` return and every awaiting
  `receive()` complete `false`
- concurrent `send()` from two threads never interleaves a line
- backpressure: with `max_queue` small and a subscriber that stops consuming,
  `publish()` blocks and resumes once the subscriber drains
