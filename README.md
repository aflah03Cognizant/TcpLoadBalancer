# tcplb — a TCP load balancer / reverse proxy in C

A single-threaded, event-driven Layer-4 (TCP) load balancer built on Linux
`epoll` and non-blocking sockets. It accepts client connections, picks a
backend (round-robin or least-connections), and pumps bytes between the two.
Backends are health-checked periodically and dead ones are taken out of
rotation automatically — all inside one thread, with no blocking calls.

This is the same architecture nginx and HAProxy use: one event loop
multiplexing thousands of connections instead of one thread per connection.

## Architecture

```
                    ┌─────────────────────────────┐
   client ────────► │   Listener socket (:8080)   │
   client ────────► │                             │
   client ────────► │   epoll_wait() event loop   │
                    │  accept + pump both ways +  │
                    │     health-check timer      │
                    └──────────────┬──────────────┘
                                   │ pick backend
                                   │ (round-robin / least-conn,
                                   │  skip UNHEALTHY)
              ┌────────────────────┼────────────────────┐
              ▼                    ▼                    ▼
        ┌───────────┐        ┌───────────┐        ┌───────────┐
        │ :9001     │        │ :9002     │        │ :9003     │
        └───────────┘        └───────────┘        └───────────┘
              ▲                    ▲                    ▲
              └── timerfd fires every 3s: probe each ───┘
                  backend with a non-blocking connect(),
                  mark UP/DOWN; the loop skips DOWN ones
```

Everything — the listener, the health timer, every client socket, every
backend socket, and every health-probe socket — is registered with a single
`epoll` instance and serviced by a single `epoll_wait()` loop.

## Files

| File | Responsibility |
|------|----------------|
| `src/main.c`       | Startup: build backend pool from config, ignore SIGPIPE, run the loop |
| `src/config.h`     | Compile-time config: listen port, strategy, backend list, health interval |
| `src/common.h`     | `ev_tag_t` (how epoll hands our own object back to us), `make_nonblocking` |
| `src/backend.[ch]` | Backend pool + the balancer (round-robin / least-connections) |
| `src/connection.[ch]` | `connection_t`: two buffered byte pipes bridging client ↔ backend |
| `src/health.[ch]`  | `timerfd`-driven, fully non-blocking health probing |
| `src/loop.[ch]`    | The epoll event loop: accept, connect, pump, backpressure, cleanup |

## Build & run (Linux — use WSL2 on Windows)

`epoll` and `timerfd` are Linux-only, so build inside WSL2 / a Linux box:

```sh
make            # produces ./tcplb
./tcplb         # listens on :8080, proxies to :9001-:9003
```

### Trying it out

Start a few backends (any TCP servers). Easy option with Python:

```sh
# three trivial HTTP backends, in separate terminals
python3 -m http.server 9001
python3 -m http.server 9002
python3 -m http.server 9003
```

Then send traffic through the proxy:

```sh
curl http://localhost:8080/     # served by one of the backends
```

Kill one backend (`Ctrl-C` on :9002) and within ~3s you'll see:

```
[health] 127.0.0.1:9002 -> DOWN
```

…and the proxy stops routing to it. Restart it and it comes back UP.

## Design notes (the interesting parts)

- **One `epoll` instance, one loop, no threads.** Per-loop cost scales with the
  number of *active* connections, not *total* — this is the fix for `select`'s
  O(n) rescans and its ~1024 fd ceiling.

- **`epoll_event.data.ptr` carries our own object back.** Each fd stores a
  pointer to a small `ev_tag_t` (`{type, owner}`). When an event fires we
  immediately know whether it's the listener, the timer, a client side, a
  backend side, or a probe — and which `connection_t`/`backend_t` it belongs
  to. No hash maps, no lookups.

- **Everything is non-blocking**, including `accept()`, `connect()` to the
  backend, and every `read()`/`write()`. A non-blocking `connect()` returns
  `EINPROGRESS`; we learn it succeeded when epoll reports the backend fd
  *writable*, then confirm with `getsockopt(SO_ERROR)`.

- **Backpressure via the interest mask.** The desired epoll mask for each fd is
  a pure function of buffer state (`compute_client_mask` / `compute_backend_mask`):
  ask to READ from a source only when its buffer is empty; ask to WRITE to a
  destination only when bytes are pending. A slow backend therefore can't make
  the loop spin, and a fast client can't overrun the fixed buffer.

- **Partial writes are handled.** `write()` can accept fewer bytes than offered
  when the send buffer fills; we keep an offset into the buffer and finish the
  write later on `EPOLLOUT`. (This is the classic proxy bug when omitted.)

- **Half-close is respected.** When one side sends EOF we flush its buffer and
  then `shutdown(SHUT_WR)` the other side, so a request/response protocol isn't
  cut off early. The connection is freed only when both directions are done.

- **Deferred free.** Both sides of a connection can appear in the same
  `epoll_wait` batch, so closing frees immediately would dangle. We mark the
  connection closed, unregister + close its fds, and free it after the batch.

- **`SIGPIPE` ignored.** Writing to a peer that closed would otherwise kill the
  process; instead we handle `EPIPE` from `write()`.

## Level-triggered vs edge-triggered

This uses **level-triggered** epoll (the default) — simpler and matches the
`select` mental model. Switching to **edge-triggered** (`EPOLLET`) would cut
syscalls but requires draining each socket until `EAGAIN` on every event; the
pump loops here are already written that way, so the change is small.

## Deliberately out of scope

Kept simple on purpose (and each is a good "what would you add next" answer):

- No TLS termination
- No HTTP parsing — it's Layer 4, so it's protocol-agnostic (a feature)
- No config file / CLI flags (backends are compiled in via `config.h`)
- No dynamic backend registration or config hot-reload
- Passive health checks (detecting failures on live traffic) — only active
  probing is implemented
```
