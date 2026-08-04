# Stdio Message Bus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `StreamMessageBus<Format>` in `src/quarkbot/common/`, an `IMessageBus` that carries messages over a pair of file descriptors (normally stdin/stdout) as JSON Lines.

**Architecture:** Three layers. `MessagePipe` owns the descriptors, the input line buffer, the write mutex and all platform-specific behaviour. `JsonMessageFormat` is a stateless policy class that encodes a `Message` to one JSON line and pull-decodes one back from a `MessagePipe`. `StreamMessageBus<Format>` glues them: it owns the subscriber registry (`MessagePublisher`) plus a blocking read loop that runs on the main thread.

**Tech Stack:** C++23, no external test framework (`src/tests/check.h` macros), `include/quarkbot/json/json.hpp` for JSON, `include/quarkbot/utils/refcnt.hpp` for payload lifetime, `basic_coro` for the awaitables.

**Spec:** `docs/superpowers/specs/2026-08-04-stdio-message-bus-design.md`

## Global Constraints

- Everything new lives in `src/quarkbot/common/` (the implementation layer). Only `base64.hpp` moves into the public SDK. Strategies never include these headers; the process that *drives* a strategy does.
- `namespace quarkbot` for all new code.
- Financial/interface conventions from `CLAUDE.md` apply, but nothing here touches `Decimal` or orders.
- No new external dependencies.
- Build: `cmake --build build -j$(nproc)`. Test: `ctest --test-dir build -R message_bus_stream --output-on-failure`.
- New `.cpp`/`.hpp` files must be added to `src/quarkbot/common/CMakeLists.txt` (`target_sources(quarkbot_impl PRIVATE ...)`).
- Test binaries link `quarkbot::sdk quarkbot::backtest` (`quarkbot::backtest` links `quarkbot::impl` publicly, which is where `common/` sources land).
- Tests include implementation headers with a relative path, e.g. `#include "../quarkbot/common/message_pipe.hpp"` — the convention in `src/tests/mem_storage_test.cpp:3` and `src/tests/logger.cpp:1`.
- Log via `quarkbot/log.hpp`: `logWarning(fmt, args...)`, `logError(fmt, args...)`. It writes to stderr, never stdout — do not add any `std::cout` output to library code.
- Wire format field names and defaults are fixed by the spec: `type`, `from`, `to`, `conv`, `schema`, `time`, `enc`, `payload`.

## File Structure

| File | Responsibility |
| --- | --- |
| `include/quarkbot/utils/base64.hpp` | **Moved** from `src/quarkbot/network/base64.hpp`, wrapped in `namespace quarkbot` |
| `src/quarkbot/network/ws.cpp` | **Modified** — include path and two call sites qualified |
| `include/quarkbot/message_bus.hpp` | **Modified** — drop the pointless `std::string(target)` copy |
| `include/quarkbot/message_bus_srl.hpp` | **Modified** — fix `send<T>`, which never compiled |
| `src/quarkbot/common/message_pipe.hpp/.cpp` | fds, line reader, mutexed writer, Windows binary mode |
| `src/quarkbot/common/message_buffer.hpp` | refcounted backing store for one decoded message |
| `src/quarkbot/common/json_message_format.hpp/.cpp` | JSON Lines encode/decode + base64 validation |
| `src/quarkbot/common/message_publisher.hpp/.cpp` | subscriber registry, per-subscriber queues, worker transfer, backpressure |
| `src/quarkbot/common/stream_message_bus.hpp` | `StreamMessageBus<Format>` template + `JsonMessageBus` alias |
| `src/tests/message_bus_stream.cpp` | all tests for the above |
| `src/tests/CMakeLists.txt` | **Modified** — register the new test |

Task order builds bottom-up so every task is independently testable: base64 move → pipe → buffer → format → publisher → bus → prerequisite fixes.

---

### Task 1: Move base64 into the SDK

**Files:**
- Create: `include/quarkbot/utils/base64.hpp` (content moved from `src/quarkbot/network/base64.hpp`)
- Delete: `src/quarkbot/network/base64.hpp`
- Modify: `src/quarkbot/network/ws.cpp:4`, `:42`, `:50`
- Test: `src/tests/message_bus_stream.cpp` (created here, extended by later tasks)
- Modify: `src/tests/CMakeLists.txt:5-25`

**Interfaces:**
- Consumes: nothing.
- Produces: `quarkbot::base64_t`, `quarkbot::base64` (constexpr, `'='` terminator), `quarkbot::base64url`. Methods: `template<typename InIter, typename OutIter> constexpr OutIter encode(InIter beg, InIter end, OutIter out) const` and the same signature for `decode`.

- [ ] **Step 1: Move the file with git so history follows**

```bash
git mv src/quarkbot/network/base64.hpp include/quarkbot/utils/base64.hpp
```

- [ ] **Step 2: Wrap the moved header in `namespace quarkbot`**

The file currently declares `base64_t` and the two constants at global scope. Add the namespace opening right after the includes at the top:

```cpp
#pragma once
#include <string_view>

namespace quarkbot {

class base64_t {
```

and close it at the very end of the file, after the `base64url` definition:

```cpp
inline constexpr auto base64url = base64_t{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",'\0'};

}
```

Leave everything between untouched.

- [ ] **Step 3: Fix the only existing consumer**

`src/quarkbot/network/ws.cpp` is in a top-level `namespace network`, *not* inside `quarkbot`, so the names need qualifying. Change line 4 from `#include "base64.hpp"` to:

```cpp
#include <quarkbot/utils/base64.hpp>
```

and both call sites (lines 42 and 50) from `base64.encode(` to `quarkbot::base64.encode(`.

- [ ] **Step 4: Create the test file with a base64 round-trip test**

Create `src/tests/message_bus_stream.cpp`:

```cpp
#include "check.h"
#include <quarkbot/utils/base64.hpp>

#include <iterator>
#include <string>
#include <string_view>

using namespace quarkbot;

static std::string b64_encode(std::string_view in) {
    std::string out;
    base64.encode(in.begin(), in.end(), std::back_inserter(out));
    return out;
}

static std::string b64_decode(std::string_view in) {
    std::string out;
    base64.decode(in.begin(), in.end(), std::back_inserter(out));
    return out;
}

static void test_base64_moved() {
    // one case per padding class, so the '=' handling is covered
    CHECK_EQUAL(b64_encode("abc"), "YWJj");        // 0 mod 3
    CHECK_EQUAL(b64_encode("ab"), "YWI=");         // 2 mod 3
    CHECK_EQUAL(b64_encode("a"), "YQ==");          // 1 mod 3
    CHECK_EQUAL(b64_encode(""), "");

    CHECK_EQUAL(b64_decode("YWJj"), "abc");
    CHECK_EQUAL(b64_decode("YWI="), "ab");
    CHECK_EQUAL(b64_decode("YQ=="), "a");

    // bytes that are not valid UTF-8 and include a NUL must survive
    std::string binary("\x00\xff\xfe\r\n\x80", 6);
    CHECK_EQUAL(b64_decode(b64_encode(binary)), binary);
}

int main() {
    test_base64_moved();
    return 0;
}
```

- [ ] **Step 5: Register the test binary**

In `src/tests/CMakeLists.txt`, add `message_bus_stream.cpp` to the `BASIC_TESTS` set (after `persistent.cpp` on line 24):

```cmake
    persistent.cpp
    message_bus_stream.cpp
)
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R message_bus_stream --output-on-failure
```

Expected: builds (including `quarkbot_network`, which the test suite force-enables, proving the `ws.cpp` fix) and the test passes.

- [ ] **Step 7: Commit**

```bash
git add include/quarkbot/utils/base64.hpp src/quarkbot/network/ws.cpp src/tests/message_bus_stream.cpp src/tests/CMakeLists.txt
git commit -m "refactor: move base64 from network component into the SDK

The stdio message bus needs base64 in src/quarkbot/common, which must not
depend on QUARKBOT_NETWORK. Wrapped in namespace quarkbot; ws.cpp is in a
top-level namespace network, so its call sites are now qualified."
```

---

### Task 2: `MessagePipe` — descriptors, line reader, mutexed writer

**Files:**
- Create: `src/quarkbot/common/message_pipe.hpp`, `src/quarkbot/common/message_pipe.cpp`
- Modify: `src/quarkbot/common/CMakeLists.txt`
- Test: `src/tests/message_bus_stream.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:

```cpp
namespace quarkbot {
class MessagePipe {
public:
    static constexpr std::size_t default_max_line = 16u*1024*1024;

    static MessagePipe stdio(std::size_t max_line = default_max_line);
    MessagePipe(int fd_in, int fd_out, std::size_t max_line = default_max_line,
                bool own_fds = false);
    MessagePipe(MessagePipe &&other);
    ~MessagePipe();

    std::optional<std::string_view> read_line();  // nullopt = EOF or over-long line
    bool line_too_long() const;
    bool write(std::string_view data);            // MT safe, whole-message atomic
    bool broken() const;
};
}
```

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/message_bus_stream.cpp` (before `main`):

```cpp
#include "../quarkbot/common/message_pipe.hpp"

#include <optional>
#include <thread>
#include <unistd.h>
#include <vector>
#include <algorithm>

namespace {

// A pipe pair wired so the test writes into what MessagePipe reads,
// and reads what MessagePipe writes.
struct PipeFixture {
    int to_pipe[2];    // test writes [1], MessagePipe reads [0]
    int from_pipe[2];  // MessagePipe writes [1], test reads [0]

    PipeFixture() {
        CHECK_EQUAL(::pipe(to_pipe), 0);
        CHECK_EQUAL(::pipe(from_pipe), 0);
    }
    ~PipeFixture() {
        for (int fd: {to_pipe[0], to_pipe[1], from_pipe[0], from_pipe[1]}) {
            if (fd >= 0) ::close(fd);
        }
    }
    MessagePipe make(std::size_t max_line = MessagePipe::default_max_line) {
        return MessagePipe(to_pipe[0], from_pipe[1], max_line);
    }
    void feed(std::string_view data) {
        CHECK_EQUAL(::write(to_pipe[1], data.data(), data.size()),
                    static_cast<ssize_t>(data.size()));
    }
    void close_feed() { ::close(to_pipe[1]); to_pipe[1] = -1; }
    std::string drain() {
        ::close(from_pipe[1]); from_pipe[1] = -1;
        std::string out;
        char buff[4096];
        for (;;) {
            auto r = ::read(from_pipe[0], buff, sizeof(buff));
            if (r <= 0) break;
            out.append(buff, static_cast<std::size_t>(r));
        }
        return out;
    }
};

}

static void test_pipe_reads_lines() {
    PipeFixture f;
    auto p = f.make();
    f.feed("first\nsecond\n");
    f.close_feed();

    auto l1 = p.read_line();
    CHECK(l1.has_value());
    CHECK_EQUAL(std::string(*l1), "first");
    auto l2 = p.read_line();
    CHECK(l2.has_value());
    CHECK_EQUAL(std::string(*l2), "second");
    CHECK(!p.read_line().has_value());
    CHECK(!p.line_too_long());
}

static void test_pipe_strips_cr_and_handles_fragments() {
    PipeFixture f;
    auto p = f.make();
    // CRLF terminator, then a line delivered in fragments with the '\n'
    // arriving on its own, then a final line with no terminator at all.
    f.feed("crlf\r\n");
    f.feed("frag");
    f.feed("mented");
    f.feed("\n");
    f.feed("no-terminator");
    f.close_feed();

    auto l1 = p.read_line();
    CHECK(l1.has_value());
    CHECK_EQUAL(std::string(*l1), "crlf");
    auto l2 = p.read_line();
    CHECK(l2.has_value());
    CHECK_EQUAL(std::string(*l2), "fragmented");
    auto l3 = p.read_line();
    CHECK(l3.has_value());
    CHECK_EQUAL(std::string(*l3), "no-terminator");
    CHECK(!p.read_line().has_value());
}

static void test_pipe_large_line() {
    PipeFixture f;
    auto p = f.make();
    std::string big(1024*1024, 'x');
    std::thread producer([&]{
        f.feed(big);
        f.feed("\n");
        f.close_feed();
    });
    auto line = p.read_line();
    producer.join();
    CHECK(line.has_value());
    CHECK_EQUAL(line->size(), big.size());
    CHECK(std::all_of(line->begin(), line->end(), [](char c){return c == 'x';}));
}

static void test_pipe_rejects_over_long_line() {
    PipeFixture f;
    auto p = f.make(16);
    f.feed("0123456789012345678901234567890123\n");
    f.close_feed();
    CHECK(!p.read_line().has_value());
    CHECK(p.line_too_long());
}

static void test_pipe_writes_atomically() {
    PipeFixture f;
    auto p = f.make();
    // Two threads writing 200 lines each; no line may be interleaved.
    std::string a(200, 'a');
    std::string b(200, 'b');
    auto writer = [&](const std::string &payload) {
        for (int i = 0; i < 200; ++i) CHECK(p.write(payload + "\n"));
    };
    std::thread t1([&]{ writer(a); });
    std::thread t2([&]{ writer(b); });
    t1.join(); t2.join();

    auto all = f.drain();
    std::size_t pos = 0, lines = 0;
    while (pos < all.size()) {
        auto nl = all.find('\n', pos);
        CHECK(nl != std::string::npos);
        auto line = all.substr(pos, nl - pos);
        CHECK(line == a || line == b);
        pos = nl + 1;
        ++lines;
    }
    CHECK_EQUAL(lines, 400u);
}

static void test_pipe_reports_broken_write() {
    PipeFixture f;
    auto p = f.make();
    ::close(f.from_pipe[0]);
    ::close(f.from_pipe[1]);
    f.from_pipe[0] = -1;
    f.from_pipe[1] = -1;
    p.write("anything\n");     // may succeed into the socket buffer or fail
    p.write("anything\n");     // by the second write the failure is certain
    CHECK(p.broken());
}
```

and call them from `main`. `test_pipe_reports_broken_write` writes to a closed
pipe, which raises `SIGPIPE` and would kill the process, so ignore it — a real
host process is expected to handle `SIGPIPE` itself, and `MessagePipe` reports
the failure through `broken()`:

```cpp
#include <csignal>

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    test_base64_moved();
    test_pipe_reads_lines();
    test_pipe_strips_cr_and_handles_fragments();
    test_pipe_large_line();
    test_pipe_rejects_over_long_line();
    test_pipe_writes_atomically();
    test_pipe_reports_broken_write();
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: FAIL — `message_pipe.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/quarkbot/common/message_pipe.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace quarkbot {

///Byte transport for a message bus - a pair of file descriptors
/**
    The reader side is single-threaded (the read loop owns it) and takes no lock.
    The writer side is MT safe and atomic per call, so concurrent senders never
    interleave a message.
 */
class MessagePipe {
public:

    static constexpr std::size_t default_max_line = 16u*1024*1024;

    ///Create a pipe over the process standard input and output
    /**
        Sets binary mode on Windows. The format never emits a bare '\n' inside a
        message, so text mode would not corrupt data, but binary mode keeps the
        written bytes exact.
     */
    static MessagePipe stdio(std::size_t max_line = default_max_line);

    ///Create a pipe over explicit descriptors
    /**
        @param fd_in descriptor to read from
        @param fd_out descriptor to write to
        @param max_line longest accepted input line; a longer line is a protocol violation
        @param own_fds when true, the destructor closes both descriptors
     */
    MessagePipe(int fd_in, int fd_out, std::size_t max_line = default_max_line,
                bool own_fds = false);
    MessagePipe(MessagePipe &&other);
    MessagePipe(const MessagePipe &) = delete;
    MessagePipe &operator=(const MessagePipe &) = delete;
    ~MessagePipe();

    ///Read one line
    /**
        Blocks until a line is available. A trailing '\r' is stripped. Content
        after the last '\n' is returned as a line when the input reaches EOF.

        @return the line without its terminator, or nothing on EOF or when the
        line exceeded max_line - use line_too_long() to tell those apart
        @note the returned view is valid until the next read_line() call
        @note reader side only, not MT safe
     */
    std::optional<std::string_view> read_line();

    ///Test whether reading stopped because of an over-long line
    bool line_too_long() const {return _line_too_long;}

    ///Write a complete message
    /**
        @param data bytes to write, including the line terminator
        @retval true written
        @retval false the output is broken; the pipe stays broken
        @note MT safe, atomic with respect to other writers
     */
    bool write(std::string_view data);

    ///Test whether a write has failed
    bool broken() const;

protected:
    int _fd_in;
    int _fd_out;
    std::size_t _max_line;
    bool _own_fds;

    std::string _in_buffer;     //unconsumed input
    std::size_t _consumed = 0;  //offset of the first unconsumed byte
    bool _eof = false;
    bool _line_too_long = false;

    mutable std::mutex _write_mx;
    bool _broken = false;

    ///read more bytes into _in_buffer
    /**
        @retval true bytes appended
        @retval false end of input
     */
    bool fill();
};

}
```

- [ ] **Step 4: Write the implementation**

Create `src/quarkbot/common/message_pipe.cpp`:

```cpp
#include "message_pipe.hpp"

#include "quarkbot/log.hpp"

#include <cerrno>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define QB_READ  _read
#define QB_WRITE _write
#define QB_CLOSE _close
#else
#include <unistd.h>
#define QB_READ  ::read
#define QB_WRITE ::write
#define QB_CLOSE ::close
#endif

namespace quarkbot {

MessagePipe MessagePipe::stdio(std::size_t max_line) {
#ifdef _WIN32
    _setmode(0, _O_BINARY);
    _setmode(1, _O_BINARY);
#endif
    return MessagePipe(0, 1, max_line, false);
}

MessagePipe::MessagePipe(int fd_in, int fd_out, std::size_t max_line, bool own_fds)
    :_fd_in(fd_in), _fd_out(fd_out), _max_line(max_line), _own_fds(own_fds) {}

MessagePipe::MessagePipe(MessagePipe &&other)
    :_fd_in(other._fd_in), _fd_out(other._fd_out), _max_line(other._max_line)
    ,_own_fds(std::exchange(other._own_fds, false))
    ,_in_buffer(std::move(other._in_buffer)), _consumed(other._consumed)
    ,_eof(other._eof), _line_too_long(other._line_too_long)
    ,_broken(other._broken) {}

MessagePipe::~MessagePipe() {
    if (_own_fds) {
        QB_CLOSE(_fd_in);
        if (_fd_out != _fd_in) QB_CLOSE(_fd_out);
    }
}

bool MessagePipe::fill() {
    char buff[8192];
    for (;;) {
        auto r = QB_READ(_fd_in, buff, sizeof(buff));
        if (r > 0) {
            _in_buffer.append(buff, static_cast<std::size_t>(r));
            return true;
        }
        if (r == 0) return false;
        if (errno == EINTR) continue;
        logError("MessagePipe: read failed: errno={}", errno);
        return false;
    }
}

std::optional<std::string_view> MessagePipe::read_line() {
    if (_line_too_long) return {};
    for (;;) {
        auto nl = _in_buffer.find('\n', _consumed);
        if (nl != std::string::npos) {
            auto begin = _consumed;
            auto end = nl;
            if (end > begin && _in_buffer[end-1] == '\r') --end;
            _consumed = nl + 1;
            //compact once the consumed prefix outweighs the rest
            if (_consumed > _in_buffer.size() / 2 && _consumed > 8192) {
                //the returned view must survive, so compact before building it
                std::string tail = _in_buffer.substr(_consumed);
                std::string line = _in_buffer.substr(begin, end - begin);
                _in_buffer = std::move(line);
                auto sz = _in_buffer.size();
                _in_buffer.append(tail);
                _consumed = sz;
                return std::string_view(_in_buffer.data(), sz);
            }
            return std::string_view(_in_buffer.data() + begin, end - begin);
        }
        if (_in_buffer.size() - _consumed > _max_line) {
            logError("MessagePipe: input line exceeds {} bytes, closing input", _max_line);
            _line_too_long = true;
            return {};
        }
        if (_eof) {
            //trailing content without a terminator is still a line
            if (_consumed < _in_buffer.size()) {
                auto begin = _consumed;
                auto end = _in_buffer.size();
                if (end > begin && _in_buffer[end-1] == '\r') --end;
                _consumed = _in_buffer.size();
                return std::string_view(_in_buffer.data() + begin, end - begin);
            }
            return {};
        }
        if (!fill()) _eof = true;
    }
}

bool MessagePipe::write(std::string_view data) {
    std::lock_guard _(_write_mx);
    if (_broken) return false;
    const char *ptr = data.data();
    std::size_t remain = data.size();
    while (remain) {
        auto r = QB_WRITE(_fd_out, ptr, static_cast<unsigned int>(remain));
        if (r > 0) {
            ptr += r;
            remain -= static_cast<std::size_t>(r);
            continue;
        }
        if (r < 0 && errno == EINTR) continue;
        logError("MessagePipe: write failed: errno={}", errno);
        _broken = true;
        return false;
    }
    return true;
}

bool MessagePipe::broken() const {
    std::lock_guard _(_write_mx);
    return _broken;
}

}
```

Note on the compaction branch: the view handed back must stay valid, so the line is moved to the front of the buffer before the view is built. That is the only correct way to compact and return a view in the same call.

- [ ] **Step 5: Add sources to the build**

In `src/quarkbot/common/CMakeLists.txt`, add inside the existing `target_sources(quarkbot_impl PRIVATE ...)` list:

```cmake
    message_pipe.hpp
    message_pipe.cpp
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R message_bus_stream --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/quarkbot/common/message_pipe.hpp src/quarkbot/common/message_pipe.cpp \
        src/quarkbot/common/CMakeLists.txt src/tests/message_bus_stream.cpp
git commit -m "feat: add MessagePipe, the byte transport for the stdio message bus

Owns the descriptor pair, the input line buffer and the write mutex. Reader
side is single-threaded and lock-free; writes are atomic per message so
concurrent senders cannot interleave a line."
```

---

### Task 3: `MessageBuffer` — payload lifetime

**Files:**
- Create: `src/quarkbot/common/message_buffer.hpp`
- Modify: `src/quarkbot/common/CMakeLists.txt`
- Test: `src/tests/message_bus_stream.cpp`

**Interfaces:**
- Consumes: `RefCountPtr`, `RefCountInstanceWithDeleter` from `quarkbot/utils/refcnt.hpp`.
- Produces:

```cpp
namespace quarkbot {
class MessageBuffer: public RefCountInstanceWithDeleter {
public:
    static RefCountPtr<MessageBuffer> create(std::string sender, std::string target,
                                             std::string payload);
    std::string_view sender() const;
    std::string_view target() const;
    std::string_view payload() const;
};
}
```

Later tasks assign `RefCountPtr<MessageBuffer>` into `Message::ownership` (type `RefCountPtr<RefCountInstanceWithDeleter>`); the converting constructor at `refcnt.hpp:40-43` handles the narrowing.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/message_bus_stream.cpp`:

```cpp
#include "../quarkbot/common/message_buffer.hpp"
#include <quarkbot/abstract/imessage_bus.hpp>

static void test_message_buffer_keeps_payload_alive() {
    Message msg;
    {
        auto buf = MessageBuffer::create("alice", "service", std::string("\x00\xff!", 3));
        msg.sender = buf->sender();
        msg.target = buf->target();
        msg.payload = buf->payload();
        msg.ownership = buf;       // converts to the base RefCountPtr
    }
    // buf is gone; ownership must still hold the buffer
    CHECK_EQUAL(std::string(msg.sender), "alice");
    CHECK_EQUAL(std::string(msg.target), "service");
    CHECK_EQUAL(msg.payload.size(), 3u);
    CHECK_EQUAL(msg.payload[1], '\xff');

    // a copy of the message shares the buffer
    Message copy = msg;
    msg = Message{};
    CHECK_EQUAL(std::string(copy.sender), "alice");
    CHECK_EQUAL(std::string(copy.payload), std::string("\x00\xff!", 3));
}
```

Call it from `main` after the pipe tests.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: FAIL — `message_buffer.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/quarkbot/common/message_buffer.hpp`:

```cpp
#pragma once

#include "quarkbot/utils/refcnt.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quarkbot {

///Refcounted backing store for the string_view fields of one Message
/**
    Three separate strings rather than one appended blob: appending to a single
    string can reallocate and dangle views that were already handed out, and
    small object optimisation covers the short fields anyway.
 */
class MessageBuffer: public RefCountInstanceWithDeleter {
public:

    static RefCountPtr<MessageBuffer> create(std::string sender, std::string target,
                                             std::string payload) {
        return RefCountPtr<MessageBuffer>(new MessageBuffer(
                std::move(sender), std::move(target), std::move(payload)));
    }

    std::string_view sender() const {return _sender;}
    std::string_view target() const {return _target;}
    std::string_view payload() const {return _payload;}

protected:
    std::string _sender;
    std::string _target;
    std::string _payload;

    MessageBuffer(std::string sender, std::string target, std::string payload)
        :RefCountInstanceWithDeleter(&deleter)
        ,_sender(std::move(sender)), _target(std::move(target))
        ,_payload(std::move(payload)) {}

    static void deleter(RefCountInstanceWithDeleter *ptr) {
        delete static_cast<MessageBuffer *>(ptr);
    }
};

}
```

- [ ] **Step 4: Add to the build and run**

Add `message_buffer.hpp` to `target_sources(quarkbot_impl PRIVATE ...)` in `src/quarkbot/common/CMakeLists.txt`, then:

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R message_bus_stream --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/common/message_buffer.hpp src/quarkbot/common/CMakeLists.txt src/tests/message_bus_stream.cpp
git commit -m "feat: add MessageBuffer, refcounted backing store for decoded messages

Message carries string_views plus an ownership RefCountPtr; this is what that
pointer holds, so queued copies of a message keep their payload alive."
```

---

### Task 4: `JsonMessageFormat` — encode

**Files:**
- Create: `src/quarkbot/common/json_message_format.hpp`, `src/quarkbot/common/json_message_format.cpp`
- Modify: `src/quarkbot/common/CMakeLists.txt`
- Test: `src/tests/message_bus_stream.cpp`

**Interfaces:**
- Consumes: `MessagePipe` (Task 2), `MessageBuffer` (Task 3), `quarkbot::base64` (Task 1).
- Produces:

```cpp
namespace quarkbot {
enum class DecodeResult { ok, skipped, eof };

class JsonMessageFormat {
public:
    static void encode(const Message &msg, std::string &out);          // appends one line incl '\n'
    static DecodeResult decode(MessagePipe &in, Message &msg);         // Task 5
    static bool is_valid_base64(std::string_view text);                // Task 5
};
}
```

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/message_bus_stream.cpp`:

```cpp
#include "../quarkbot/common/json_message_format.hpp"
#include <quarkbot/json/json.hpp>
#include <chrono>
#include <cstdint>

static Json encode_to_json(const Message &msg) {
    std::string line;
    JsonMessageFormat::encode(msg, line);
    CHECK(!line.empty());
    CHECK_EQUAL(line.back(), '\n');
    // exactly one line: no interior newline may leak from the payload
    CHECK_EQUAL(line.find('\n'), line.size()-1);
    return Json::from_string(std::string_view(line).substr(0, line.size()-1));
}

static void test_encode_text_payload() {
    Message msg;
    msg.type = MessageType::normal_message;
    msg.target = "storage-replica";
    msg.payload = "hello \xc4\x8d\xc3\xa1st";     // valid UTF-8
    msg.conversation_id = 12345;
    msg.schema = 0x9f2c1a3b7d004e11ULL;
    msg.send_time = std::chrono::system_clock::time_point(std::chrono::milliseconds(1784700083313));

    auto j = encode_to_json(msg);
    CHECK_EQUAL(std::string(j["type"].as_text()), "normal");
    CHECK_EQUAL(std::string(j["to"].as_text()), "storage-replica");
    CHECK_EQUAL(std::string(j["conv"].as_text()), "12345");
    CHECK_EQUAL(std::string(j["schema"].as_text()), "9f2c1a3b7d004e11");
    CHECK_EQUAL(j["time"].as<std::int64_t>(), 1784700083313LL);
    CHECK(!j["enc"].as_bool());
    CHECK_EQUAL(std::string(j["payload"].as_text()), "hello \xc4\x8d\xc3\xa1st");
    // empty fields are omitted entirely
    CHECK(j["from"].is_null());
}

static void test_encode_binary_payload_uses_base64() {
    std::string binary("\x00\xff\xfe\r\n\x80", 6);
    Message msg;
    msg.target = "peer";
    msg.payload = binary;

    auto j = encode_to_json(msg);
    CHECK(j["enc"].as_bool());
    CHECK_EQUAL(std::string(j["payload"].as_text()), b64_encode(binary));
    // conv and schema are zero, so they are omitted
    CHECK(j["conv"].is_null());
    CHECK(j["schema"].is_null());
}

static void test_encode_type_names() {
    struct { MessageType type; const char *name; } cases[] = {
        {MessageType::normal_message, "normal"},
        {MessageType::announce, "announce"},
        {MessageType::add_to_group, "add_to_group"},
        {MessageType::group_close, "group_close"},
        {MessageType::no_route, "no_route"},
    };
    for (const auto &c: cases) {
        Message msg;
        msg.type = c.type;
        auto j = encode_to_json(msg);
        CHECK_EQUAL(std::string(j["type"].as_text()), std::string(c.name));
    }
}

static void test_encode_max_uint64_fields() {
    Message msg;
    msg.conversation_id = UINT64_MAX;
    msg.schema = UINT64_MAX;
    auto j = encode_to_json(msg);
    CHECK_EQUAL(std::string(j["conv"].as_text()), "18446744073709551615");
    CHECK_EQUAL(std::string(j["schema"].as_text()), "ffffffffffffffff");
}
```

Call all four from `main`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: FAIL — `json_message_format.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/quarkbot/common/json_message_format.hpp`:

```cpp
#pragma once

#include "message_pipe.hpp"

#include "quarkbot/abstract/imessage_bus.hpp"

#include <string>
#include <string_view>

namespace quarkbot {

///Outcome of reading one message from a pipe
enum class DecodeResult {
    ///a message was decoded
    ok,
    ///the line was malformed and was skipped; keep reading
    skipped,
    ///no more input
    eof
};

///JSON Lines wire format - one JSON object per line
/**
    Chosen because it has no length-counted raw bytes: Json::write_string escapes
    every control character, so a serialized message is always exactly one line.
    That survives Windows stdio text mode, and a JavaScript peer needs only
    readline plus JSON.parse.

    Fields: type, from, to, conv, schema, time, enc, payload. Every field has a
    default, so "{}" is a valid empty normal message.
 */
class JsonMessageFormat {
public:

    ///Encode one message as a single line, terminator included
    /**
        @param msg message to encode
        @param out buffer to append to; existing content is kept
     */
    static void encode(const Message &msg, std::string &out);

    ///Read and decode one message
    /**
        @param in pipe to read from
        @param msg receives the message; its ownership field receives the buffer
        backing the string_view fields
        @return ok, skipped (malformed line) or eof
     */
    static DecodeResult decode(MessagePipe &in, Message &msg);

    ///Test whether text is well formed standard base64
    /**
        base64_t::decode silently skips characters outside the charset and can
        never report failure, so input has to be validated separately.
     */
    static bool is_valid_base64(std::string_view text);
};

}
```

- [ ] **Step 4: Write the encode implementation**

Create `src/quarkbot/common/json_message_format.cpp`:

```cpp
#include "json_message_format.hpp"

#include "message_buffer.hpp"

#include "quarkbot/json/json.hpp"
#include "quarkbot/log.hpp"
#include "quarkbot/utils/base64.hpp"

#include <charconv>
#include <format>
#include <iterator>

namespace quarkbot {

static constexpr std::string_view type_names[] = {
    "normal", "announce", "add_to_group", "group_close", "no_route"
};

static std::string_view type_to_name(MessageType type) {
    auto idx = static_cast<std::size_t>(type);
    if (idx >= std::size(type_names)) return type_names[0];
    return type_names[idx];
}

///Test whether the bytes form valid UTF-8
/**
    Only well formed sequences may go on the wire as a JSON string; anything else
    is base64 encoded. Rejects overlong encodings, surrogates and out of range
    code points, so the peer always receives decodable text.
 */
static bool is_valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        std::size_t len;
        unsigned int cp;
        if (c < 0x80) {i += 1; continue;}
        else if ((c & 0xE0) == 0xC0) {len = 2; cp = static_cast<unsigned int>(c & 0x1F);}
        else if ((c & 0xF0) == 0xE0) {len = 3; cp = static_cast<unsigned int>(c & 0x0F);}
        else if ((c & 0xF8) == 0xF0) {len = 4; cp = static_cast<unsigned int>(c & 0x07);}
        else return false;
        if (i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k) {
            auto cc = static_cast<unsigned char>(s[i+k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | static_cast<unsigned int>(cc & 0x3F);
        }
        if (len == 2 && cp < 0x80) return false;
        if (len == 3 && cp < 0x800) return false;
        if (len == 4 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += len;
    }
    return true;
}

void JsonMessageFormat::encode(const Message &msg, std::string &out) {
    Json j;
    j.set("type", Json(type_to_name(msg.type)));
    if (!msg.sender.empty()) j.set("from", Json(msg.sender));
    if (!msg.target.empty()) j.set("to", Json(msg.target));
    if (msg.conversation_id) j.set("conv", Json(std::format("{}", msg.conversation_id)));
    if (msg.schema) j.set("schema", Json(std::format("{:016x}", msg.schema)));

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            msg.send_time.time_since_epoch()).count();
    j.set("time", Json(ms));

    if (!msg.payload.empty()) {
        if (is_valid_utf8(msg.payload)) {
            j.set("payload", Json(msg.payload));
        } else {
            std::string encoded;
            encoded.reserve((msg.payload.size() + 2) / 3 * 4);
            base64.encode(msg.payload.begin(), msg.payload.end(),
                          std::back_inserter(encoded));
            j.set("enc", Json(true));
            j.set("payload", Json(encoded));
        }
    }

    j.serialize([&](char c){out.push_back(c);});
    out.push_back('\n');
}

}
```

- [ ] **Step 5: Add to the build and run**

Add `json_message_format.hpp` and `json_message_format.cpp` to `target_sources(quarkbot_impl PRIVATE ...)`, then:

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R message_bus_stream --output-on-failure
```

Expected: PASS. If `Json::set` ordering puts `enc` after `payload`, that is fine — object member order is not part of the format.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/common/json_message_format.hpp src/quarkbot/common/json_message_format.cpp \
        src/quarkbot/common/CMakeLists.txt src/tests/message_bus_stream.cpp
git commit -m "feat: encode Message to a JSON line

Text payloads go through as JSON strings, binary payloads as base64 with
enc:true. conv and schema are strings because they use the full uint64 range;
time is a number because unix milliseconds fits a JS double."
```

---

### Task 5: `JsonMessageFormat` — decode

**Files:**
- Modify: `src/quarkbot/common/json_message_format.cpp`
- Test: `src/tests/message_bus_stream.cpp`

**Interfaces:**
- Consumes: everything from Task 4.
- Produces: working `JsonMessageFormat::decode` and `JsonMessageFormat::is_valid_base64`.

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/message_bus_stream.cpp`:

```cpp
namespace {

// Decode a fixed set of lines through a real pipe.
struct DecodeFixture {
    PipeFixture f;
    std::optional<MessagePipe> pipe;

    explicit DecodeFixture(std::string_view input) {
        pipe.emplace(f.make());
        f.feed(input);
        f.close_feed();
    }
    DecodeResult next(Message &msg) {
        return JsonMessageFormat::decode(*pipe, msg);
    }
};

}

static void test_decode_round_trip() {
    struct Case {
        const char *name;
        MessageType type;
        std::string sender;
        std::string target;
        std::string payload;
        ConversationID conv;
        srl::SchemaHash schema;
    };
    const std::string binary("\x00\xff\xfe\r\n\x80", 6);
    Case cases[] = {
        {"ascii text",   MessageType::normal_message, "a", "b", "hello", 1, 2},
        {"utf-8 text",   MessageType::announce,       "", "svc", "\xc4\x8dau", 0, 0},
        {"binary",       MessageType::normal_message, "s", "t", binary, 7, 8},
        {"empty fields", MessageType::group_close,    "", "", "", 0, 0},
        {"max ids",      MessageType::no_route,       "x", "y", "z", UINT64_MAX, UINT64_MAX},
        {"pad 1 mod 3",  MessageType::normal_message, "", "t", std::string("\xff", 1), 0, 0},
        {"pad 2 mod 3",  MessageType::normal_message, "", "t", std::string("\xff\xfe", 2), 0, 0},
        {"pad 0 mod 3",  MessageType::normal_message, "", "t", std::string("\xff\xfe\xfd", 3), 0, 0},
    };

    for (const auto &c: cases) {
        Message src;
        src.type = c.type;
        src.sender = c.sender;
        src.target = c.target;
        src.payload = c.payload;
        src.conversation_id = c.conv;
        src.schema = c.schema;
        src.send_time = std::chrono::system_clock::time_point(std::chrono::milliseconds(1784700083313));

        std::string line;
        JsonMessageFormat::encode(src, line);

        DecodeFixture f(line);
        Message got;
        CHECK_PRINT(f.next(got) == DecodeResult::ok, c.name);
        CHECK_PRINT(got.type == c.type, c.name);
        CHECK_PRINT(std::string(got.sender) == c.sender, c.name);
        CHECK_PRINT(std::string(got.target) == c.target, c.name);
        CHECK_PRINT(std::string(got.payload) == c.payload, c.name);
        CHECK_PRINT(got.conversation_id == c.conv, c.name);
        CHECK_PRINT(got.schema == c.schema, c.name);
        CHECK_PRINT(got.send_time == src.send_time, c.name);
    }
}

static void test_decode_skips_malformed_and_continues() {
    const char *bad[] = {
        "not json at all",
        "[1,2,3]",
        "42",
        R"({"type":"nonsense"})",
        R"({"enc":true,"payload":"!!!not base64!!!"})",
    };
    for (const char *line: bad) {
        std::string input(line);
        input += "\n";
        input += R"({"to":"after","payload":"ok"})";
        input += "\n";

        DecodeFixture f(input);
        Message msg;
        CHECK_PRINT(f.next(msg) == DecodeResult::skipped, line);
        CHECK_PRINT(f.next(msg) == DecodeResult::ok, line);
        CHECK_PRINT(std::string(msg.target) == "after", line);
        CHECK_PRINT(f.next(msg) == DecodeResult::eof, line);
    }
}

static void test_decode_accepts_sparse_messages() {
    auto before = std::chrono::system_clock::now();
    DecodeFixture f("{}\n"
                    R"({"to":"t","unknown_member":[1,2],"payload":"p"})" "\n");
    Message msg;

    CHECK(f.next(msg) == DecodeResult::ok);
    CHECK(msg.type == MessageType::normal_message);      // type defaults to normal
    CHECK(msg.sender.empty());
    CHECK(msg.target.empty());
    CHECK(msg.payload.empty());
    CHECK_EQUAL(msg.conversation_id, 0u);
    CHECK_GREATER_EQUAL(msg.send_time, before);          // missing time -> receive time

    CHECK(f.next(msg) == DecodeResult::ok);              // unknown members ignored
    CHECK_EQUAL(std::string(msg.target), "t");
    CHECK_EQUAL(std::string(msg.payload), "p");

    CHECK(f.next(msg) == DecodeResult::eof);
}

static void test_decode_reports_eof_on_empty_input() {
    DecodeFixture f("");
    Message msg;
    CHECK(f.next(msg) == DecodeResult::eof);
}

static void test_is_valid_base64() {
    CHECK(JsonMessageFormat::is_valid_base64(""));
    CHECK(JsonMessageFormat::is_valid_base64("YWJj"));
    CHECK(JsonMessageFormat::is_valid_base64("YWI="));
    CHECK(JsonMessageFormat::is_valid_base64("YQ=="));
    CHECK(!JsonMessageFormat::is_valid_base64("YWJ"));      // length not a multiple of 4
    CHECK(!JsonMessageFormat::is_valid_base64("YW=J"));     // '=' not at the end
    CHECK(!JsonMessageFormat::is_valid_base64("YQ==="));    // three pad characters
    CHECK(!JsonMessageFormat::is_valid_base64("YW J"));     // space is not in the charset
    CHECK(!JsonMessageFormat::is_valid_base64("YW-J"));     // base64url charset is not accepted
}
```

Call all five from `main`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: FAIL at link — undefined reference to `JsonMessageFormat::decode` and `::is_valid_base64`.

- [ ] **Step 3: Implement `is_valid_base64` and `decode`**

Append to `src/quarkbot/common/json_message_format.cpp`, inside `namespace quarkbot`:

```cpp
bool JsonMessageFormat::is_valid_base64(std::string_view text) {
    if (text.size() % 4) return false;
    std::size_t pad = 0;
    while (pad < 2 && pad < text.size() && text[text.size()-1-pad] == '=') ++pad;
    auto body = text.substr(0, text.size() - pad);
    for (char c: body) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
               || (c >= '0' && c <= '9') || c == '+' || c == '/';
        if (!ok) return false;
    }
    return true;
}

///Parse an unsigned 64 bit value from text
/**
    Json's own integral conversion goes through strtoll and would wrap values
    above INT64_MAX, so conv and schema are parsed here explicitly.
 */
static bool parse_u64(std::string_view text, int base, std::uint64_t &out) {
    if (text.empty()) return false;
    auto r = std::from_chars(text.data(), text.data()+text.size(), out, base);
    return r.ec == std::errc{} && r.ptr == text.data()+text.size();
}

static bool name_to_type(std::string_view name, MessageType &out) {
    for (std::size_t i = 0; i < std::size(type_names); ++i) {
        if (type_names[i] == name) {
            out = static_cast<MessageType>(i);
            return true;
        }
    }
    return false;
}

DecodeResult JsonMessageFormat::decode(MessagePipe &in, Message &msg) {
    auto line = in.read_line();
    if (!line) return DecodeResult::eof;

    Json j;
    try {
        j = Json::from_string(*line);
    } catch (const Json::ParseError &) {
        logWarning("JsonMessageFormat: line is not valid JSON, skipped ({} bytes)", line->size());
        return DecodeResult::skipped;
    }
    if (!j.is_object()) {
        logWarning("JsonMessageFormat: line is not a JSON object, skipped");
        return DecodeResult::skipped;
    }

    Message out;

    const auto &jtype = j["type"];
    if (jtype.is_null()) {
        out.type = MessageType::normal_message;
    } else if (!name_to_type(jtype.as_text(), out.type)) {
        logWarning("JsonMessageFormat: unknown message type '{}', skipped", jtype.as_text());
        return DecodeResult::skipped;
    }

    const auto &jconv = j["conv"];
    if (!jconv.is_null() && !parse_u64(jconv.as_text(), 10, out.conversation_id)) {
        logWarning("JsonMessageFormat: conv is not a number, skipped");
        return DecodeResult::skipped;
    }
    const auto &jschema = j["schema"];
    if (!jschema.is_null() && !parse_u64(jschema.as_text(), 16, out.schema)) {
        logWarning("JsonMessageFormat: schema is not a hex number, skipped");
        return DecodeResult::skipped;
    }

    const auto &jtime = j["time"];
    if (jtime.is_null()) {
        out.send_time = std::chrono::system_clock::now();
    } else {
        out.send_time = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(jtime.as<std::int64_t>()));
    }

    std::string payload;
    auto raw = j["payload"].as_text();
    if (j["enc"].as_bool()) {
        if (!is_valid_base64(raw)) {
            logWarning("JsonMessageFormat: payload is not valid base64, skipped");
            return DecodeResult::skipped;
        }
        payload.reserve(raw.size() / 4 * 3);
        base64.decode(raw.begin(), raw.end(), std::back_inserter(payload));
    } else {
        payload.assign(raw);
    }

    auto buf = MessageBuffer::create(std::string(j["from"].as_text()),
                                    std::string(j["to"].as_text()),
                                    std::move(payload));
    out.sender = buf->sender();
    out.target = buf->target();
    out.payload = buf->payload();
    out.ownership = std::move(buf);

    msg = std::move(out);
    return DecodeResult::ok;
}
```

`Json::as_text()` returns a `std::string_view` into the `Json` object, which is why every field is copied into the `MessageBuffer` before `j` goes out of scope.

- [ ] **Step 4: Run tests**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R message_bus_stream --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/common/json_message_format.cpp src/tests/message_bus_stream.cpp
git commit -m "feat: decode a JSON line into a Message

Every field has a default, so {} is a valid message and a router developer can
hand-feed lines. Malformed lines are skipped with a warning rather than
terminating the read loop. base64 is validated up front because base64_t
silently skips invalid characters."
```

---

### Task 6: `MessagePublisher` — subscribers, worker transfer, backpressure

**Files:**
- Create: `src/quarkbot/common/message_publisher.hpp`, `src/quarkbot/common/message_publisher.cpp`
- Modify: `src/quarkbot/common/CMakeLists.txt`
- Test: `src/tests/message_bus_stream.cpp`

**Interfaces:**
- Consumes: `Message`, `IEventStream<Message>`, `EventStreamStoppable<Message>`, `PExecutionWorker`.
- Produces:

```cpp
namespace quarkbot {
class MessagePublisher: public std::enable_shared_from_this<MessagePublisher> {
public:
    static std::shared_ptr<MessagePublisher> create(std::size_t max_queue);
    std::shared_ptr<IEventStream<Message> > subscribe();
    void publish(const Message &msg);   // blocks while a queue is full
    void close();                       // every awaiter completes false
    void wake();                        // abandon a blocked publish()
};
}
```

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/message_bus_stream.cpp`:

```cpp
#include "../quarkbot/common/message_publisher.hpp"
#include "../quarkbot/common/thread_executor.hpp"
#include <quarkbot/event_stream.hpp>
#include <quarkbot/execution_worker.hpp>
#include <quarkbot/strategy_fragment.hpp>
#include <atomic>

namespace {

Message make_msg(std::string_view target, std::string_view payload) {
    Message m;
    auto buf = MessageBuffer::create({}, std::string(target), std::string(payload));
    m.target = buf->target();
    m.payload = buf->payload();
    m.ownership = std::move(buf);
    m.send_time = std::chrono::system_clock::now();
    return m;
}

}

// Collect n messages, then read once more and record whether the stream closed.
static quarkbot::StrategyFragment collect(EventStream<Message> stream,
                                          std::vector<std::string> *out,
                                          int expected,
                                          std::atomic<bool> *closed) {
    Message msg;
    for (int i = 0; i < expected; ++i) {
        bool ok = co_await stream.receive(msg);
        CHECK(ok);
        out->push_back(std::string(msg.payload));
    }
    bool ok = co_await stream.receive(msg);
    if (closed) closed->store(!ok);
    co_return;
}

static void test_publisher_delivers_to_all_subscribers() {
    ExecutionWorker worker(ThreadExecutor::create());
    auto pub = MessagePublisher::create(100);

    std::vector<std::string> got_a, got_b;
    std::atomic<bool> closed_a{false}, closed_b{false};
    auto sub_a = worker.launch(collect(EventStream<Message>(pub->subscribe()),
                                       &got_a, 2, &closed_a));
    auto sub_b = worker.launch(collect(EventStream<Message>(pub->subscribe()),
                                       &got_b, 2, &closed_b));

    pub->publish(make_msg("t", "one"));
    pub->publish(make_msg("t", "two"));
    pub->close();

    coro::sync_await(sub_a);
    coro::sync_await(sub_b);

    CHECK_EQUAL(got_a.size(), 2u);
    CHECK_EQUAL(got_a[0], "one");
    CHECK_EQUAL(got_a[1], "two");
    CHECK_EQUAL(got_b.size(), 2u);
    CHECK_EQUAL(got_b[1], "two");
    CHECK(closed_a.load());
    CHECK(closed_b.load());
}

static void test_publisher_queues_messages_sent_before_receive() {
    ExecutionWorker worker(ThreadExecutor::create());
    auto pub = MessagePublisher::create(100);
    auto stream = pub->subscribe();

    // publish first, subscribe-and-consume second: nothing may be lost
    pub->publish(make_msg("t", "early-1"));
    pub->publish(make_msg("t", "early-2"));

    std::vector<std::string> got;
    std::atomic<bool> closed{false};
    auto task = worker.launch(collect(EventStream<Message>(stream), &got, 2, &closed));
    pub->close();
    coro::sync_await(task);

    CHECK_EQUAL(got.size(), 2u);
    CHECK_EQUAL(got[0], "early-1");
    CHECK_EQUAL(got[1], "early-2");
}

static void test_publisher_resumes_on_subscriber_worker() {
    ExecutionWorker worker(ThreadExecutor::create());
    auto pub = MessagePublisher::create(100);

    std::atomic<std::thread::id> resume_thread{};
    auto fragment = [](EventStream<Message> stream,
                       std::atomic<std::thread::id> *where) -> quarkbot::StrategyFragment {
        Message msg;
        bool ok = co_await stream.receive(msg);
        CHECK(ok);
        where->store(std::this_thread::get_id());
        ok = co_await stream.receive(msg);
        co_return;
    };
    auto task = worker.launch(fragment(EventStream<Message>(pub->subscribe()), &resume_thread));

    // give the fragment time to suspend, so publish() takes the resume path
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto publisher_thread = std::this_thread::get_id();
    pub->publish(make_msg("t", "x"));
    pub->close();
    coro::sync_await(task);

    CHECK(resume_thread.load() != std::thread::id{});
    CHECK(resume_thread.load() != publisher_thread);
}

static void test_publisher_closed_subscriber_does_not_block_others() {
    ExecutionWorker worker(ThreadExecutor::create());
    auto pub = MessagePublisher::create(2);

    auto dead = pub->subscribe();
    dead->close();                 // never consumes; must not fill up and block

    std::vector<std::string> got;
    std::atomic<bool> closed{false};
    auto task = worker.launch(collect(EventStream<Message>(pub->subscribe()),
                                      &got, 3, &closed));
    pub->publish(make_msg("t", "1"));
    pub->publish(make_msg("t", "2"));
    pub->publish(make_msg("t", "3"));
    pub->close();
    coro::sync_await(task);

    CHECK_EQUAL(got.size(), 3u);
}

static void test_publisher_applies_backpressure() {
    ExecutionWorker worker(ThreadExecutor::create());
    auto pub = MessagePublisher::create(2);
    auto stream = pub->subscribe();

    std::atomic<int> published{0};
    std::thread producer([&]{
        for (int i = 0; i < 6; ++i) {
            pub->publish(make_msg("t", std::to_string(i)));
            published.fetch_add(1);
        }
    });

    // with a queue cap of 2 and nobody consuming, the producer must stall
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_LESS_EQUAL(published.load(), 3);

    std::vector<std::string> got;
    std::atomic<bool> closed{false};
    auto task = worker.launch(collect(EventStream<Message>(stream), &got, 6, &closed));
    producer.join();
    pub->close();
    coro::sync_await(task);

    CHECK_EQUAL(published.load(), 6);
    CHECK_EQUAL(got.size(), 6u);
    CHECK_EQUAL(got[0], "0");
    CHECK_EQUAL(got[5], "5");
}

static void test_publisher_wake_abandons_blocked_publish() {
    auto pub = MessagePublisher::create(1);
    auto stream = pub->subscribe();     // never consumed

    std::atomic<bool> done{false};
    std::thread producer([&]{
        for (int i = 0; i < 10; ++i) pub->publish(make_msg("t", "x"));
        done.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!done.load());
    pub->wake();                        // release the blocked publish
    producer.join();
    CHECK(done.load());
}
```

Call all six from `main`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: FAIL — `message_publisher.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/quarkbot/common/message_publisher.hpp`:

```cpp
#pragma once

#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/execution_worker.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace quarkbot {

///Fan-out of received messages to subscribed streams
/**
    One mutex guards everything: the subscriber list, every queue and every
    awaiter slot. Messages arrive at pipe rate, not market data rate, so
    contention is not a concern, and a single lock rules out lock inversion
    between the publisher and its subscribers.

    Queues are unbounded in the sense that nothing is ever dropped - control
    messages must not be lost - but publish() blocks while any queue holds
    max_queue messages, so the peer feels backpressure through the pipe.
 */
class MessagePublisher: public std::enable_shared_from_this<MessagePublisher> {
public:

    static std::shared_ptr<MessagePublisher> create(std::size_t max_queue) {
        return std::shared_ptr<MessagePublisher>(new MessagePublisher(max_queue));
    }

    ///Create a new subscribed stream
    /**
        @note MT safe
     */
    std::shared_ptr<IEventStream<Message> > subscribe();

    ///Deliver a message to every subscriber
    /**
        Blocks while any subscriber queue is full, until it drains or until
        wake() or close() is called.
        @note MT safe
     */
    void publish(const Message &msg);

    ///Close every subscribed stream
    /**
        Awaiting coroutines are resumed with false. Also releases a blocked
        publish().
        @note MT safe
     */
    void close();

    ///Release a blocked publish() without closing
    /**
        @note MT safe
     */
    void wake();

    class Stream;

protected:
    explicit MessagePublisher(std::size_t max_queue):_max_queue(max_queue) {}

    std::mutex _mx;
    std::condition_variable _drain;
    std::vector<std::weak_ptr<Stream> > _subscribers;
    std::size_t _max_queue;
    bool _closed = false;
    bool _wake = false;
    bool _warned_full = false;

    void unsubscribe(Stream *s);

    friend class Stream;
};

}
```

- [ ] **Step 4: Write the implementation**

Create `src/quarkbot/common/message_publisher.cpp`:

```cpp
#include "message_publisher.hpp"

#include "quarkbot/log.hpp"

#include <algorithm>

namespace quarkbot {

///One subscribed stream
/**
    Owns no mutex - the publisher's lock covers its queue and awaiter slot.
 */
class MessagePublisher::Stream: public EventStreamStoppable<Message> {
public:

    explicit Stream(std::shared_ptr<MessagePublisher> pub):_pub(std::move(pub)) {}
    ~Stream() {close();}

    virtual bool is_open() const override {
        std::unique_lock lk(_pub->_mx);
        return !_closed;
    }

    virtual void close() override {
        awaitable<bool>::result pending;
        PExecutionWorker worker;
        {
            std::unique_lock lk(_pub->_mx);
            if (_closed) return;
            _closed = true;
            pending = std::move(_awaiting);
            worker = std::move(_worker);
            _awaiting_value = nullptr;
            _queue.clear();
        }
        _pub->unsubscribe(this);
        _pub->wake();                  //a full queue may have been blocking publish()
        resume_with(std::move(pending), std::move(worker), false);
    }

    virtual bool current(Message &) override {
        return false;                  //a queue has no current value
    }

    virtual coro::awaitable<bool> receive(Message &ref) override {
        {
            std::unique_lock lk(_pub->_mx);
            if (pop_lk(ref)) {
                lk.unlock();
                _pub->_drain.notify_all();
                return true;
            }
            if (_closed) return false;
        }
        return [&ref, this](auto promise) {
            coro::prepared_coro out;
            std::unique_lock lk(_pub->_mx);
            if (pop_lk(ref)) {
                lk.unlock();
                _pub->_drain.notify_all();
                out = promise(true);
            } else if (_closed) {
                lk.unlock();
                out = promise(false);
            } else {
                _awaiting_value = &ref;
                _awaiting = std::move(promise);
                _worker = IExecutionWorker::current();
            }
            return out;
        };
    }

    virtual coro::awaitable<bool> receive(Message &ref, std::size_t &missed) override {
        missed = 0;                    //nothing is ever dropped
        return receive(ref);
    }

    ///Hand a message over; called with the publisher lock held
    /**
        @param msg message
        @param pending receives an awaiter that must be resumed after unlocking
        @param worker receives the awaiter's worker
        @retval true the message was taken (queued or handed to an awaiter)
        @retval false the stream is closed
     */
    bool deliver_lk(const Message &msg, awaitable<bool>::result &pending,
                    PExecutionWorker &worker) {
        if (_closed) return false;
        if (_awaiting_value) {
            *_awaiting_value = msg;
            _awaiting_value = nullptr;
            pending = std::move(_awaiting);
            worker = std::move(_worker);
        } else {
            _queue.push_back(msg);
        }
        return true;
    }

    bool full_lk(std::size_t max_queue) const {
        return !_closed && _queue.size() >= max_queue;
    }

    ///Resume a coroutine on its own worker
    /**
        ~prepared_coro() resumes inline, so the value must be handed to the
        worker rather than discarded - otherwise the strategy would run on the
        publishing thread.
     */
    static void resume_with(awaitable<bool>::result pending, PExecutionWorker worker,
                            bool value) {
        if (!pending) return;
        auto pc = pending(value);
        if (!pc) return;
        if (worker) worker->resume(pc.release());
    }

protected:
    std::shared_ptr<MessagePublisher> _pub;
    std::deque<Message> _queue;
    awaitable<bool>::result _awaiting = {};
    Message *_awaiting_value = nullptr;
    PExecutionWorker _worker = {};
    bool _closed = false;

    ///take the front of the queue; called with the publisher lock held
    bool pop_lk(Message &ref) {
        if (_queue.empty()) return false;
        ref = std::move(_queue.front());
        _queue.pop_front();
        return true;
    }
};

std::shared_ptr<IEventStream<Message> > MessagePublisher::subscribe() {
    auto s = std::make_shared<Stream>(shared_from_this());
    std::unique_lock lk(_mx);
    if (_closed) {
        lk.unlock();
        s->close();
        return s;
    }
    _subscribers.push_back(s);
    return s;
}

void MessagePublisher::unsubscribe(Stream *s) {
    std::unique_lock lk(_mx);
    auto end = std::remove_if(_subscribers.begin(), _subscribers.end(),
            [&](const std::weak_ptr<Stream> &w){
        auto p = w.lock();
        return !p || p.get() == s;
    });
    _subscribers.erase(end, _subscribers.end());
}

void MessagePublisher::publish(const Message &msg) {
    //hold the resumes until the lock is released
    struct Pending {
        std::shared_ptr<Stream> stream;
        awaitable<bool>::result result;
        PExecutionWorker worker;
    };
    std::vector<Pending> pending;

    {
        std::unique_lock lk(_mx);
        if (_closed) return;

        std::vector<std::shared_ptr<Stream> > live;
        live.reserve(_subscribers.size());
        for (auto &w: _subscribers) {
            if (auto p = w.lock()) live.push_back(std::move(p));
        }

        for (auto &s: live) {
            Pending p;
            if (s->deliver_lk(msg, p.result, p.worker)) {
                if (p.result) {
                    p.stream = s;
                    pending.push_back(std::move(p));
                }
            }
        }

        //backpressure: wait until every queue is below the limit
        _drain.wait(lk, [&]{
            if (_closed || _wake) return true;
            bool any_full = std::any_of(live.begin(), live.end(),
                    [&](const std::shared_ptr<Stream> &s){
                return s->full_lk(_max_queue);
            });
            if (any_full && !_warned_full) {
                _warned_full = true;
                logWarning("MessagePublisher: a subscriber queue reached {} messages, "
                           "reading is paused until it drains", _max_queue);
            }
            return !any_full;
        });
        _wake = false;
    }

    for (auto &p: pending) {
        Stream::resume_with(std::move(p.result), std::move(p.worker), true);
    }
}

void MessagePublisher::close() {
    std::vector<std::shared_ptr<Stream> > live;
    {
        std::unique_lock lk(_mx);
        if (_closed) return;
        _closed = true;
        for (auto &w: _subscribers) {
            if (auto p = w.lock()) live.push_back(std::move(p));
        }
        _subscribers.clear();
    }
    _drain.notify_all();
    for (auto &s: live) s->close();
}

void MessagePublisher::wake() {
    {
        std::unique_lock lk(_mx);
        _wake = true;
    }
    _drain.notify_all();
}

}
```

Two subtleties worth understanding before editing this file:

- `publish()` resumes awaiters **after** releasing the lock, and holds a `shared_ptr` to each stream in `pending` so the stream cannot be destroyed between the unlock and the resume.
- The `_drain.wait` predicate calls `full_lk` on streams while the lock is held, which is exactly the invariant `full_lk` documents. Do not call it from anywhere else.

- [ ] **Step 5: Add sources to the build and run**

Add `message_publisher.hpp` and `message_publisher.cpp` to `target_sources(quarkbot_impl PRIVATE ...)`, then:

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R message_bus_stream --output-on-failure
```

Expected: PASS. If `test_publisher_applies_backpressure` hangs, the `_drain.wait` predicate or the `notify_all` in `receive()` is wrong — the consumer must notify after every pop.

- [ ] **Step 6: Commit**

```bash
git add src/quarkbot/common/message_publisher.hpp src/quarkbot/common/message_publisher.cpp \
        src/quarkbot/common/CMakeLists.txt src/tests/message_bus_stream.cpp
git commit -m "feat: add MessagePublisher with worker transfer and backpressure

One mutex covers the subscriber list, all queues and all awaiter slots, which
removes any chance of lock inversion. Awaiters are resumed on their own
execution worker, never inline on the publishing thread, and publish() blocks
while a queue is full so the peer feels real backpressure."
```

---

### Task 7: `StreamMessageBus<Format>` and the read loop

**Files:**
- Create: `src/quarkbot/common/stream_message_bus.hpp`
- Modify: `src/quarkbot/common/CMakeLists.txt`
- Test: `src/tests/message_bus_stream.cpp`

**Interfaces:**
- Consumes: `MessagePipe` (Task 2), `JsonMessageFormat`/`DecodeResult` (Tasks 4-5), `MessagePublisher` (Task 6).
- Produces:

```cpp
namespace quarkbot {
template<typename Format>
class StreamMessageBus: public IMessageBus {
public:
    struct Config { std::size_t max_queue = 1000; };
    StreamMessageBus(MessagePipe pipe, Config cfg = {});
    std::shared_ptr<IEventStream<Message> > subscribe() override;
    void send(const Message &msg) override;
    void run();
    void request_stop();
};
using JsonMessageBus = StreamMessageBus<JsonMessageFormat>;
}
```

- [ ] **Step 1: Write the failing tests**

Append to `src/tests/message_bus_stream.cpp`:

```cpp
#include "../quarkbot/common/stream_message_bus.hpp"
#include <quarkbot/message_bus.hpp>

static void test_bus_send_writes_a_line() {
    PipeFixture f;
    JsonMessageBus bus(f.make());
    bus.send(make_msg("peer", "hello"));
    bus.send(make_msg("peer", std::string("\x00\xff", 2)));

    auto out = f.drain();
    std::size_t lines = 0, pos = 0;
    while (pos < out.size()) {
        auto nl = out.find('\n', pos);
        CHECK(nl != std::string::npos);
        auto j = Json::from_string(std::string_view(out).substr(pos, nl-pos));
        CHECK_EQUAL(std::string(j["to"].as_text()), "peer");
        pos = nl+1;
        ++lines;
    }
    CHECK_EQUAL(lines, 2u);
}

static void test_bus_read_loop_publishes_and_closes() {
    PipeFixture f;
    ExecutionWorker worker(ThreadExecutor::create());
    JsonMessageBus bus(f.make());

    std::vector<std::string> got;
    std::atomic<bool> closed{false};
    auto task = worker.launch(collect(EventStream<Message>(bus.subscribe()),
                                      &got, 2, &closed));

    f.feed(R"({"to":"t","payload":"one"})" "\n");
    f.feed("garbage that is not json\n");
    f.feed(R"({"to":"t","payload":"two"})" "\n");
    f.close_feed();

    bus.run();          // returns at EOF
    coro::sync_await(task);

    CHECK_EQUAL(got.size(), 2u);
    CHECK_EQUAL(got[0], "one");
    CHECK_EQUAL(got[1], "two");
    CHECK(closed.load());        // EOF closed the stream
}

static void test_bus_end_to_end_over_two_pipes() {
    // A -> B and B -> A, each bus owning one direction of each pipe
    int ab[2], ba[2];
    CHECK_EQUAL(::pipe(ab), 0);
    CHECK_EQUAL(::pipe(ba), 0);

    ExecutionWorker worker(ThreadExecutor::create());
    JsonMessageBus bus_a(MessagePipe(ba[0], ab[1]));
    JsonMessageBus bus_b(MessagePipe(ab[0], ba[1]));

    std::vector<std::string> got_b;
    std::atomic<bool> closed_b{false};
    auto task_b = worker.launch(collect(EventStream<Message>(bus_b.subscribe()),
                                        &got_b, 1, &closed_b));

    std::thread reader_b([&]{ bus_b.run(); });

    bus_a.send(make_msg("service-b", "ping"));
    ::close(ab[1]);                    // A closes its write end -> B sees EOF
    reader_b.join();
    coro::sync_await(task_b);

    CHECK_EQUAL(got_b.size(), 1u);
    CHECK_EQUAL(got_b[0], "ping");
    CHECK(closed_b.load());

    ::close(ab[0]);
    ::close(ba[0]);
    ::close(ba[1]);
}

static void test_bus_request_stop_releases_backpressure() {
    PipeFixture f;
    JsonMessageBus bus(f.make(), JsonMessageBus::Config{.max_queue = 1});
    auto stream = bus.subscribe();     // never consumed, so the queue fills

    for (int i = 0; i < 20; ++i) {
        f.feed(R"({"to":"t","payload":"x"})" "\n");
    }
    std::thread loop([&]{ bus.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    bus.request_stop();                // must unblock the stalled publish()
    loop.join();                       // and let run() return
    f.close_feed();
}

static void test_bus_wraps_into_MessageBus_and_sends() {
    PipeFixture f;
    auto bus = std::make_shared<JsonMessageBus>(f.make());
    MessageBus wrapper(bus);
    wrapper.send_raw("peer", "raw payload", 42, 0xabcdefULL);

    auto out = f.drain();
    auto nl = out.find('\n');
    CHECK(nl != std::string::npos);
    auto j = Json::from_string(std::string_view(out).substr(0, nl));
    CHECK_EQUAL(std::string(j["to"].as_text()), "peer");
    CHECK_EQUAL(std::string(j["payload"].as_text()), "raw payload");
    CHECK_EQUAL(std::string(j["conv"].as_text()), "42");
    CHECK_EQUAL(std::string(j["schema"].as_text()), "0000000000abcdef");
}
```

Call all five from `main`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: FAIL — `stream_message_bus.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/quarkbot/common/stream_message_bus.hpp`:

```cpp
#pragma once

#include "json_message_format.hpp"
#include "message_pipe.hpp"
#include "message_publisher.hpp"

#include "quarkbot/abstract/imessage_bus.hpp"
#include "quarkbot/log.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace quarkbot {

///Message bus carried over a pair of file descriptors
/**
    Intended for a strategy driven by an external router or orchestrator: the
    process reads commands from its standard input and writes messages to its
    standard output.

    The read loop is blocking and belongs on the main thread, which has nothing
    else to do once the strategy and its worker are running. Shutdown is the peer
    closing our input; request_stop() only takes effect between messages, because
    interrupting a blocking read is not portable.

    @tparam Format wire format policy - see JsonMessageFormat
 */
template<typename Format>
class StreamMessageBus: public IMessageBus {
public:

    struct Config {
        ///messages one subscriber may have pending before reading pauses
        std::size_t max_queue = 1000;
    };

    StreamMessageBus(MessagePipe pipe, Config cfg = {})
        :_pipe(std::move(pipe)), _publisher(MessagePublisher::create(cfg.max_queue)) {}

    virtual std::shared_ptr<IEventStream<Message> > subscribe() override {
        return _publisher->subscribe();
    }

    ///Encode and write a message
    /**
        @note MT safe. A broken output is reported once and then silently
        ignored; the read loop is asked to stop.
     */
    virtual void send(const Message &msg) override {
        if (_broken.load(std::memory_order_relaxed)) return;
        thread_local std::string buffer;
        buffer.clear();
        Format::encode(msg, buffer);
        if (!_pipe.write(buffer)) {
            if (!_broken.exchange(true)) {
                logError("StreamMessageBus: output is broken, no more messages will be sent");
            }
            request_stop();
        }
    }

    ///Run the read loop
    /**
        Blocks until the input reaches its end, a protocol violation occurs or
        request_stop() is observed. Closes every subscribed stream before
        returning, so awaiting strategies wake up with false.

        @note call once; not reentrant
     */
    void run() {
        while (!_stop.load(std::memory_order_relaxed)) {
            Message msg;
            auto r = Format::decode(_pipe, msg);
            if (r == DecodeResult::eof) break;
            if (r == DecodeResult::skipped) continue;
            _publisher->publish(msg);
        }
        _publisher->close();
    }

    ///Ask the read loop to stop
    /**
        Observed between messages only. Also releases a publish() that is
        blocked on backpressure.
        @note MT safe
     */
    void request_stop() {
        _stop.store(true, std::memory_order_relaxed);
        _publisher->wake();
    }

protected:
    MessagePipe _pipe;
    std::shared_ptr<MessagePublisher> _publisher;
    std::atomic<bool> _stop = {false};
    std::atomic<bool> _broken = {false};
};

///The bus a router or orchestrator talks to: JSON Lines over stdin/stdout
using JsonMessageBus = StreamMessageBus<JsonMessageFormat>;

}
```

- [ ] **Step 4: Add to the build and run**

Add `stream_message_bus.hpp` to `target_sources(quarkbot_impl PRIVATE ...)`, then:

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R message_bus_stream --output-on-failure
```

Expected: PASS.

One thing to check while running: `run()` calls `publish()`, which can block on backpressure. `request_stop()` sets `_stop` *and* wakes the publisher, so `test_bus_request_stop_releases_backpressure` proves the stalled publish is released and the loop exits. If it hangs, `wake()` is not reaching the `_drain` predicate.

- [ ] **Step 5: Commit**

```bash
git add src/quarkbot/common/stream_message_bus.hpp src/quarkbot/common/CMakeLists.txt \
        src/tests/message_bus_stream.cpp
git commit -m "feat: add StreamMessageBus, an IMessageBus over a descriptor pair

Blocking read loop for the main thread, MT-safe synchronous send, and EOF on
the input closing every subscribed stream. JsonMessageBus is the JSON Lines
instantiation a router or orchestrator talks to."
```

---

### Task 8: Fix `MessageBus::send<T>` and the `send_raw` copy

**Files:**
- Modify: `include/quarkbot/message_bus_srl.hpp:25-29`
- Modify: `include/quarkbot/message_bus.hpp:36-46`
- Test: `src/tests/message_bus_stream.cpp`

**Interfaces:**
- Consumes: `JsonMessageBus` (Task 7), `srl::serialize_to`, `srl::schema_hash`.
- Produces: working `MessageBus::send<T>(target, payload, conversation_id)`.

- [ ] **Step 1: Write the failing test**

Append to `src/tests/message_bus_stream.cpp`:

```cpp
#include <quarkbot/message_bus_srl.hpp>

namespace {

struct TestPayload {
    int a;
    std::string b;

    template<typename Self>
    auto fields(this Self &self) {
        return std::tie(self.a, self.b);
    }
};

}

static void test_message_bus_typed_send_round_trip() {
    PipeFixture f;
    auto bus = std::make_shared<JsonMessageBus>(f.make());
    MessageBus wrapper(bus);

    TestPayload sent{42, "hello"};
    wrapper.send("peer", sent, 7);

    auto out = f.drain();
    auto nl = out.find('\n');
    CHECK(nl != std::string::npos);

    // feed the produced line back through a decoder and extract the value
    PipeFixture back;
    auto pipe = back.make();
    back.feed(std::string_view(out).substr(0, nl+1));
    back.close_feed();

    Message msg;
    CHECK(JsonMessageFormat::decode(pipe, msg) == DecodeResult::ok);
    CHECK_EQUAL(std::string(msg.target), "peer");
    CHECK_EQUAL(msg.conversation_id, 7u);
    CHECK_EQUAL(msg.schema, srl::schema_hash<TestPayload>);

    TestPayload got{};
    CHECK(msg.extract(got));
    CHECK_EQUAL(got.a, 42);
    CHECK_EQUAL(got.b, "hello");
}
```

Call it from `main`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -30
```

Expected: FAIL to compile inside `message_bus_srl.hpp` — `srl::schema_hash<T>()` calls a variable template, and `std::move(payload)` cannot initialise the `std::string_view` parameter.

- [ ] **Step 3: Fix `MessageBus::send<T>`**

In `include/quarkbot/message_bus_srl.hpp`, replace the body of `send`:

```cpp
template<typename T>
inline void MessageBus::send(std::string_view target, const T &payload, ConversationID conversation_id) {
    std::vector<std::uint8_t> s;
    srl::serialize_to<std::uint8_t>(payload, std::back_inserter(s));
    send_raw(target,
             std::string_view(reinterpret_cast<const char *>(s.data()), s.size()),
             conversation_id,
             srl::schema_hash<std::decay_t<T> >);
}
```

Three changes: the serialized bytes `s` are sent instead of the original object, `schema_hash` is used as the variable template it is (matching `Message::extract` at line 12), and `std::decay_t` keeps the hash consistent with `extract`.

- [ ] **Step 4: Drop the needless copy in `send_raw`**

In `include/quarkbot/message_bus.hpp`, `send_raw` builds a `std::string(target)` to initialise a `std::string_view` field — an allocation whose only purpose is to be destroyed at the end of the call. Replace the `_ptr->send({...})` argument list:

```cpp
        void send_raw(std::string_view target, std::string_view payload,
                ConversationID conversation_id = {},
                srl::SchemaHash schema = {}) {
            _ptr->send({
                MessageType::normal_message,
                {},
                target,
                payload,
                conversation_id,
                schema,
                std::chrono::system_clock::now(),
            });
        }
```

- [ ] **Step 5: Run tests**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure
```

Expected: the whole suite passes — run all tests here, not just the new one, since `message_bus.hpp` is a public SDK header.

- [ ] **Step 6: Commit**

```bash
git add include/quarkbot/message_bus_srl.hpp include/quarkbot/message_bus.hpp src/tests/message_bus_stream.cpp
git commit -m "fix: make MessageBus::send<T> compile and stop copying the target

send<T> forwarded the original object instead of the serialized bytes and
called schema_hash, a variable template, as a function. Neither showed up
because the template was never instantiated. send_raw also allocated a
std::string only to initialise a string_view field."
```

---

### Task 9: Document the wire format for peer implementers

**Files:**
- Create: `docs/message_bus_protocol.md`
- Test: none (documentation)

**Interfaces:**
- Consumes: the format as implemented in Tasks 4-5.
- Produces: nothing code depends on.

- [ ] **Step 1: Write the document**

Create `docs/message_bus_protocol.md`. It must be complete enough that someone writing the router in JavaScript never needs to read the C++:

```markdown
# Message bus wire protocol

A strategy process speaks this protocol on its standard input and output. One
message per line, UTF-8, `\n`-terminated, each line a single JSON object.
Standard error carries the log and is never part of the protocol.

## Fields

| field | JSON type | meaning | default |
| --- | --- | --- | --- |
| `type` | string | `normal`, `announce`, `add_to_group`, `group_close`, `no_route` | `normal` |
| `from` | string | sender id; empty from a plain node, filled in by a router | empty |
| `to` | string | target id, service name or group name | empty |
| `conv` | string | conversation id, decimal, full uint64 range | `"0"` |
| `schema` | string | payload schema hash, 16 lowercase hex digits | `"0"` |
| `time` | number | send time, unix milliseconds | receive time |
| `enc` | bool | `true` when `payload` is base64 | `false` |
| `payload` | string | message body | empty |

`conv` and `schema` are strings because they use the full uint64 range and JSON
numbers lose precision above 2^53. `time` is a number: unix milliseconds fits a
double comfortably, so `new Date(msg.time)` just works.

`enc` is `false` when the payload is text and its bytes go through verbatim, and
`true` when the payload is arbitrary binary, encoded as standard base64 with `=`
padding. A sender should only set `enc` when the payload is not valid UTF-8.

Every field has a default, so `{}` is a valid empty `normal` message.

## Robustness

- Unknown object members are ignored, so new fields can be added without
  breaking either side.
- An unparseable line, a JSON value that is not an object, an unknown `type`
  and an invalid base64 payload are all logged and skipped. The reader keeps
  going, so a stray diagnostic line on the peer's stdout does not kill the
  connection.
- A line longer than 16 MiB is a protocol violation and terminates the reader.
- The strategy exits its read loop when its standard input reaches end of file.
  That is the intended shutdown signal.

## Minimal Node.js peer

```js
import { createInterface } from 'node:readline';
import { spawn } from 'node:child_process';

const child = spawn('./build/bin/my_strategy', [], {
  stdio: ['pipe', 'pipe', 'inherit'],   // stderr passes through: it is the log
});

function send(msg) {
  child.stdin.write(JSON.stringify({ time: Date.now(), ...msg }) + '\n');
}

createInterface({ input: child.stdout }).on('line', (line) => {
  let msg;
  try { msg = JSON.parse(line); } catch { return; }   // skip, do not crash
  const payload = msg.enc
    ? Buffer.from(msg.payload ?? '', 'base64')
    : (msg.payload ?? '');
  handle(msg.to, payload, msg.conv, msg.schema);
});

send({ type: 'announce', from: 'router', to: String(Date.now() + 60000),
       payload: 'at your service' });

// shutting the strategy down: close its input
// child.stdin.end();
```
```

- [ ] **Step 2: Verify the document against the code**

Re-read `src/quarkbot/common/json_message_format.cpp` and confirm every field name, every default and every skip rule in the table matches the implementation. Fix the document, not the code, if they disagree — unless the code is wrong, in which case fix the code and say so in the commit message.

- [ ] **Step 3: Commit**

```bash
git add docs/message_bus_protocol.md
git commit -m "docs: describe the message bus wire protocol

Enough detail to implement a router in another language without reading the
C++, including a minimal Node.js peer."
```

---

## Verification

After Task 9, confirm the whole thing from a clean build:

```bash
rm -rf build && mkdir -p build && cd build && cmake .. && cmake --build . -j$(nproc)
ctest --test-dir . --output-on-failure
```

Every test must pass, including the pre-existing suite — Tasks 1 and 8 touch shared code (`ws.cpp`, `message_bus.hpp`).

## Known limitations, by design

- `request_stop()` is observed between messages only; a read blocked mid-line
  ends when the peer closes the input. Documented in the spec's *Read loop and
  shutdown* section.
- A strategy that abandons its stream without closing it stalls the read loop.
  This is deliberate: a wedged instance is detectable and killable, silent
  memory growth is not.
- Only `JsonMessageFormat` exists. A binary format can be added as a second
  `Format` policy when a peer needs one; it would also need `read_exact()` on
  `MessagePipe`.
- `QueueEventPublisher` is left broken and unused — see the spec's *Out of
  scope* section for the list of its defects, which deserves its own ticket.
