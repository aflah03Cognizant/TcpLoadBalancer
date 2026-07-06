# tcplb — A Complete Beginner's Guide

This guide assumes you have **almost no programming experience**. We'll start
from "what is a variable" and build all the way up to understanding every line
of the load balancer. Take it slow. Read the "C basics" section first — the
rest depends on it.

---

## Table of contents

1. [What does this program even do?](#1-what-does-this-program-even-do)
2. [C basics you must know first](#2-c-basics-you-must-know-first)
3. [The big picture: how the files fit together](#3-the-big-picture-how-the-files-fit-together)
4. [File-by-file walkthrough (beginner level)](#4-file-by-file-walkthrough)
5. [How one request flows through everything](#5-how-one-request-flows-through-everything)
6. [The connection state machine (memorize this)](#6-the-connection-state-machine)
7. [Mock interview Q&A sheet](#7-mock-interview-qa-sheet)

---

## 1. What does this program even do?

Imagine a busy restaurant with **one receptionist** at the front door and
**three identical kitchens** in the back.

- Customers (we call them **clients**) walk in the front door.
- The receptionist (our **load balancer**) sends each customer's order to one
  of the three kitchens (the **backend servers**).
- The receptionist spreads orders out so no single kitchen is overwhelmed
  (this is **load balancing**).
- Every few seconds the receptionist checks "is each kitchen still open?"
  (**health checking**). If a kitchen catches fire, the receptionist stops
  sending orders there until it recovers.

Our program is that receptionist, but for **network connections**. It listens
on a network port (`8080`), and whenever a program connects to it, it forwards
all the bytes back and forth to one of the backend servers. It never looks at
*what* the bytes mean — it just shuffles them. That's why it's called a
**TCP proxy** (TCP is the basic protocol that carries the bytes).

> **Key word: "port".** A computer has one network address (like a building),
> but thousands of numbered "doors" called ports. A program can "listen" on a
> port to receive connections. Web servers usually use port 80 or 443; we use
> 8080.

---

## 2. C basics you must know first

C is the language this is written in. Here are the only concepts you need.

### 2.1 A program is a list of instructions

The computer runs your instructions top to bottom. Instructions live inside
**functions**. Every C program starts running at a special function called
`main`.

```c
int main(void) {
    // instructions go here, they run in order
    return 0;   // "0" tells the operating system "I finished successfully"
}
```
- `//` starts a **comment** — text the computer ignores, written for humans.
- `/* ... */` is a comment that can span multiple lines.

### 2.2 Variables and types

A **variable** is a named box that holds a value. In C you must say what
*type* of value goes in the box.

```c
int   port = 8080;      // "int" = a whole number
char  letter = 'A';     // "char" = a single character (really a small number)
```
Common types you'll see:
- `int` — a whole number (e.g. `5`, `-3`, `8080`).
- `char` — one byte; used for characters and for raw data bytes.
- `size_t` — a whole number used for *sizes and counts* (never negative).
- `uint32_t` — a number that is exactly 32 bits, always positive.
- `void` — means "nothing" (e.g. a function that returns nothing).

### 2.3 Functions

A **function** is a named, reusable block of instructions. It can take
**inputs** (called parameters) and give back one **output** (the return value).

```c
int add(int a, int b) {   // takes two ints, returns an int
    return a + b;
}
// somewhere else:
int result = add(2, 3);   // result is now 5
```
Reading `int add(int a, int b)`:
- `int` (first word) = the type it returns.
- `add` = the function's name.
- `(int a, int b)` = the inputs it needs.

### 2.4 Pointers (the scary one — but it's simple)

A **pointer** is a variable that holds the *address* of another variable —
like writing down *where* something lives instead of the thing itself.

Analogy: instead of carrying a house, you carry a slip of paper with the
house's street address. That slip is a pointer.

```c
int  x = 10;      // a box holding 10
int *p = &x;      // p holds the ADDRESS of x. The '&' means "address of".
                  // The '*' in "int *p" means "p is a pointer to an int".

int y = *p;       // '*p' means "go to that address and get the value" = 10.
                  // This is called "dereferencing".
```
Why do we need them? Two big reasons:
1. **To let a function change something.** Normally a function gets *copies* of
   its inputs, so changes don't stick. Pass a pointer, and the function can
   change the original.
2. **To refer to big things cheaply.** Instead of copying a huge chunk of data,
   we pass its address (tiny).

You'll see `NULL` a lot — it's a special pointer value meaning "points to
nothing" (an empty slip of paper).

### 2.5 Structs (grouping related data)

A **struct** bundles several variables into one named package.

```c
struct backend {
    char host[64];   // the address text, e.g. "127.0.0.1"
    int  port;       // e.g. 9001
    int  status;     // is it healthy?
};
```
Now `struct backend` is a new type. You access its parts with a dot:

```c
struct backend b;
b.port = 9001;          // set the "port" field
```
If you have a **pointer to a struct**, you use `->` instead of `.`:

```c
struct backend *bp = &b;
bp->port = 9001;        // same as (*bp).port = 9001
```
> Remember: **dot (`.`)** when you have the struct itself; **arrow (`->`)**
> when you have a pointer to it. That's the only difference.

### 2.6 Enums (a list of named choices)

An **enum** is a set of named labels. Under the hood they're just numbers, but
the names make the code readable.

```c
enum status { HEALTHY, UNHEALTHY };   // HEALTHY is 0, UNHEALTHY is 1
```

### 2.7 Arrays

An **array** is a numbered row of boxes of the same type.

```c
int nums[3] = { 10, 20, 30 };   // nums[0]=10, nums[1]=20, nums[2]=30
```
Counting starts at **0**, not 1. So a 3-element array has indexes 0, 1, 2.

### 2.8 The `#` lines (preprocessor)

Lines starting with `#` are handled *before* real compilation — think
find-and-replace and copy-paste.

```c
#include "backend.h"   // paste the contents of backend.h right here
#define PORT 8080       // everywhere it sees "PORT", write "8080" instead
```

### 2.9 `typedef` (nicknames for types)

`typedef` gives a type a shorter nickname so you don't repeat `struct` everywhere.

```c
typedef struct backend backend_t;
// now you can write "backend_t" instead of "struct backend"
```

### 2.10 Header files (`.h`) vs source files (`.c`)

- A **`.h` file (header)** is like a *table of contents*: it announces what
  functions and types exist, so other files know they can use them.
- A **`.c` file (source)** contains the *actual code* — the function bodies.

They pair up: `backend.h` announces, `backend.c` implements. Other files
`#include "backend.h"` to use those functions.

That's all the C you need. Now the real thing.

---

## 3. The big picture: how the files fit together

Our project has these files (all under `src/`):

```
main.c        ← where the program starts. Sets things up, then starts the loop.
config.h      ← the settings: which port, which backends, which strategy.
common.h      ← tiny shared definitions used everywhere.
backend.c/.h  ← the list of backend servers + the logic to pick one.
connection.c/.h ← represents one client<->backend conversation.
health.c/.h   ← periodically checks if backends are alive.
loop.c/.h     ← THE ENGINE: the never-ending loop that does all the work.
```

How they depend on each other (arrows mean "uses"):

```
        main.c
          │  (starts)
          ▼
        loop.c ────────► connection.c ──► backend.c
          │                                  ▲
          └────────► health.c ───────────────┘
          
   (common.h and config.h are shared by everyone)
```

The **one idea** that ties it all together: there is a single "engine" (in
`loop.c`) that sits and waits for *events* (a new customer arrives, data is
ready, a timer ticks). Each time something happens, the engine reacts, then
goes back to waiting. Everything else (`backend.c`, `connection.c`, `health.c`)
is a helper the engine calls.

---

## 4. File-by-file walkthrough

We'll go in the order that makes sense for *learning*, simplest first.

---

### 4.1 `common.h` — shared definitions

```c
#ifndef COMMON_H
#define COMMON_H
...
#endif
```
This wrapper is called an **include guard**. If this file gets pasted in twice
(which happens easily), the guard makes the second paste do nothing. Without
it, you'd get "defined twice" errors. Every `.h` file has this pattern. Just
recognize it and move on.

```c
typedef enum {
    EV_LISTENER,   // the "front door" — a new customer is knocking
    EV_TIMER,      // the clock ticked — time to check on the kitchens
    EV_CLIENT,     // a customer sent us something
    EV_BACKEND,    // a kitchen sent us something
    EV_PROBE       // a health-check test finished
} ev_type_t;
```
This is a list of the **kinds of events** our engine can receive. When the
engine wakes up, the first thing it asks is "what kind of event is this?" and
this enum gives the possible answers.

```c
typedef struct ev_tag {
    ev_type_t type;    // one of the five kinds above
    void     *owner;   // a pointer to the thing this event is about
} ev_tag_t;
```
This little struct is a **name tag**. Every network connection we track gets a
name tag attached. When an event happens, the system hands us back the name
tag, and it instantly tells us (1) what kind of event and (2) which specific
connection or backend it's about.

`void *owner` means "a pointer to *something* — I'll figure out the exact type
from the `type` field." (`void *` is C's way of saying "pointer to anything.")

> **Why this matters (plain English):** imagine 5,000 people are talking to our
> receptionist at once. When one of them says something, how do we know *which*
> conversation it belongs to, instantly, without searching through all 5,000?
> Answer: we stapled a name tag to each conversation, and the system hands the
> tag right back to us. That's `ev_tag_t`.

```c
int make_nonblocking(int fd);
```
This just *announces* a function (the real code is in `loop.c`). "fd" means
**file descriptor** — a number the operating system gives you to refer to an
open connection or file. (More on this below.)

> **What's a "file descriptor" (fd)?** When you open a file or a network
> connection, the operating system doesn't give you the thing itself — it gives
> you a **number** (like a coat-check ticket). You use that number for all
> future operations: "read from fd 5", "write to fd 7". Sockets (network
> connections) are file descriptors too.

---

### 4.2 `config.h` — the settings

```c
#define LISTEN_PORT          8080          // the door we listen on
#define LB_STRATEGY          LB_LEAST_CONN // how we pick a backend
#define HEALTH_INTERVAL_SEC  3             // check backends every 3 seconds
```
These are just settings. `#define` means "wherever you see this name, paste
this value." Change them and rebuild to reconfigure.

```c
static const struct {
    const char *host;
    int         port;
} BACKENDS[] = {
    { "127.0.0.1", 9001 },
    { "127.0.0.1", 9002 },
    { "127.0.0.1", 9003 },
};
```
This is the **list of backend servers**, written out by hand. It's an array
where each item has a `host` (address text) and a `port`. So we have three
backends, all on this machine (`127.0.0.1` means "this same computer"), on
ports 9001, 9002, 9003.

```c
#define NUM_BACKENDS ((int)(sizeof(BACKENDS) / sizeof(BACKENDS[0])))
```
A trick to count the array automatically. `sizeof` tells you how many bytes
something takes. Total size of the array divided by the size of one item = the
number of items. So if you add a fourth backend, this number updates by itself.

---

### 4.3 `backend.h` and `backend.c` — the servers and how we pick one

**`backend.h`** describes what a backend server looks like:

```c
typedef struct backend {
    char host[64];              // e.g. "127.0.0.1"
    int  port;                  // e.g. 9001
    struct sockaddr_in addr;    // the same address in the binary form the OS needs
    backend_status_t status;    // HEALTHY or UNHEALTHY
    int active_connections;     // how many customers are currently using it
    // ...health-check bookkeeping...
} backend_t;
```
Think of one `backend_t` as an index card for one kitchen: its address, whether
it's open, and how busy it is right now.

There's also a **pool**, which is just "all the index cards together plus some
bookkeeping":

```c
typedef struct backend_pool {
    backend_t    *items;      // the array of backends
    int           count;      // how many we have
    lb_strategy_t strategy;   // round-robin or least-connections
    int           rr_next;    // a counter used by round-robin
} backend_pool_t;
```

Now **`backend.c`** — the actual logic.

```c
int backend_pool_init(backend_pool_t *pool, lb_strategy_t strategy)
{
    pool->items = calloc(MAX_BACKENDS, sizeof(backend_t));
    if (!pool->items) return -1;
    pool->count = 0;
    pool->strategy = strategy;
    pool->rr_next = 0;
    return 0;
}
```
This **sets up an empty pool**. `calloc` asks the operating system for enough
memory to hold up to `MAX_BACKENDS` (32) index cards, and clears it to zeros.
If the computer is out of memory, `calloc` returns `NULL`, and we return `-1`
to signal failure. Otherwise we set the counters to their starting values and
return `0` (success).

> **Convention:** in C, functions often return `0` for success and `-1` for
> failure. You'll see this everywhere.

```c
int backend_pool_add(backend_pool_t *pool, const char *host, int port)
{
    if (pool->count >= MAX_BACKENDS) return -1;   // no room left
    backend_t *b = &pool->items[pool->count];     // point at the next empty card
    memset(b, 0, sizeof(*b));                      // wipe it clean
    strncpy(b->host, host, sizeof(b->host) - 1);   // copy in the address text
    b->port = port;
    b->probe_fd = -1;                              // -1 means "no health-test running"
    b->status = BACKEND_HEALTHY;                   // assume it's up to begin with
    ...
    inet_pton(AF_INET, host, &b->addr.sin_addr);   // turn "127.0.0.1" into binary
    ...
    pool->count++;                                 // we now have one more backend
    return 0;
}
```
This **adds one backend** to the pool. Line by line:
- Check there's room.
- `&pool->items[pool->count]` gets the address of the next unused slot.
- `memset(..., 0, ...)` zeroes it (a clean start).
- `strncpy` copies the address text safely (the "n" version won't overflow).
- We record the port and mark the backend HEALTHY to begin with.
- `inet_pton` converts the human text `"127.0.0.1"` into the numeric form the
  network needs (like translating a word into a barcode).
- Increment the count.

```c
backend_t *backend_pick(backend_pool_t *pool)
{
    ...
    if (pool->strategy == LB_ROUND_ROBIN) {
        for (int i = 0; i < pool->count; i++) {
            int idx = (pool->rr_next + i) % pool->count;
            if (pool->items[idx].status == BACKEND_HEALTHY) {
                pool->rr_next = (idx + 1) % pool->count;
                return &pool->items[idx];
            }
        }
        return NULL;
    }
    ...
}
```
This is **the load-balancing decision** — which kitchen gets the next order.

**Round-robin** = take turns. `rr_next` remembers whose turn is next. We start
there and walk forward looking for a healthy backend. The `%` (modulo) makes
the counting wrap around: after the last backend, we go back to the first
(like a clock going from 12 back to 1). When we find a healthy one, we remember
to start *after* it next time, and return it. If none are healthy, return
`NULL` (nothing available).

```c
    backend_t *best = NULL;
    for (int i = 0; i < pool->count; i++) {
        backend_t *b = &pool->items[i];
        if (b->status != BACKEND_HEALTHY) continue;   // skip dead ones
        if (!best || b->active_connections < best->active_connections)
            best = b;
    }
    return best;
```
**Least-connections** = give the order to the *least busy* healthy kitchen. We
look at every healthy backend and keep the one with the fewest
`active_connections`. `continue` means "skip the rest of this loop turn." `!best`
is true only for the very first candidate (before we've picked anyone).

```c
void backend_set_status(backend_t *b, backend_status_t status)
{
    if (b->status != status)
        fprintf(stderr, "[health] %s:%d -> %s\n", ...);   // print only on change
    b->status = status;
}
```
Marks a backend healthy or unhealthy, and prints a message *only when the
status actually changes* (so we don't spam the screen). `fprintf(stderr, ...)`
prints text to the screen (specifically the "error output" stream, standard for
logs).

---

### 4.4 `connection.h` and `connection.c` — one conversation

A **connection** is one customer talking to one kitchen through us. We sit in
the middle copying bytes both ways.

```c
#define BUF_SIZE 65536   // 64 kilobytes
```
A **buffer** is a temporary holding area for bytes. This says each holding area
is 64 KB.

```c
typedef struct buffer {
    char   data[BUF_SIZE];  // the actual bytes
    size_t len;             // how many bytes are in here right now
    size_t off;             // how many of them we've already sent onward
} buffer_t;
```
Picture a bucket. `data` is the bucket. `len` is how full it is. `off` is how
much we've already poured out. So the amount still waiting to be poured is
`len - off`.

```c
static inline size_t buffer_pending(const buffer_t *b) { return b->len - b->off; }
static inline int    buffer_empty  (const buffer_t *b) { return b->len == b->off; }
```
Two tiny helper functions: "how much is left to send?" and "is it empty?"
(`inline` just means "this is so small, paste it directly instead of making a
real function call" — a speed optimization; don't worry about it.)

```c
struct connection {
    int        client_fd;    // the customer's connection number
    int        backend_fd;   // the kitchen's connection number
    backend_t *backend;      // which kitchen we chose
    ev_tag_t   client_tag;   // name tag for the customer side
    ev_tag_t   backend_tag;  // name tag for the kitchen side
    buffer_t   c2b;          // bucket for customer -> kitchen bytes
    buffer_t   b2c;          // bucket for kitchen -> customer bytes
    uint32_t   client_events;   // what we're currently listening for on the customer side
    uint32_t   backend_events;  // ...and on the kitchen side
    int connecting;   // are we still dialing the kitchen?
    int client_eof;   // did the customer hang up their sending?
    int backend_eof;  // did the kitchen hang up its sending?
    int closed;       // is this conversation over?
    connection_t *next_free;  // used when cleaning up (explained later)
};
```
This is the **complete record of one conversation**. Two connection numbers
(customer + kitchen), which kitchen we picked, two buckets (one for each
direction), and some yes/no flags tracking the conversation's state.

`c2b` = "**c**lient **to** **b**ackend" (what the customer says).
`b2c` = "**b**ackend **to** **c**lient" (what the kitchen says back).

```c
connection_t *connection_create(int client_fd, int backend_fd, backend_t *b)
{
    connection_t *c = calloc(1, sizeof(*c));   // ask for memory, cleared to 0
    if (!c) return NULL;
    c->client_fd  = client_fd;
    c->backend_fd = backend_fd;
    c->backend    = b;
    c->client_tag.type   = EV_CLIENT;  c->client_tag.owner  = c;
    c->backend_tag.type  = EV_BACKEND; c->backend_tag.owner = c;
    return c;
}
```
Creates a new conversation record. It gets memory (`calloc`), stores the two
connection numbers and the chosen kitchen, and fills in the two **name tags**
so they point back to this very conversation (`owner = c`). That way, when an
event fires later, we can find our way back to this record.

---

### 4.5 `health.c` — is each backend alive?

Every few seconds we test each backend by trying to briefly connect to it. If
the connection works, it's alive; if it's refused, it's dead.

```c
int health_timer_create(int interval_sec)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    ...
    its.it_value.tv_sec    = interval_sec;   // first alarm after N seconds
    its.it_interval.tv_sec = interval_sec;   // then repeat every N seconds
    timerfd_settime(tfd, 0, &its, NULL);
    return tfd;
}
```
This creates a **repeating alarm clock**. The clever part: on Linux, a timer can
be a "file descriptor" (a connection number) just like a network socket. So our
engine can watch the clock the exact same way it watches network connections.
`interval_sec` is 3, so the alarm rings every 3 seconds.

```c
static void start_probe(int epfd, backend_t *b)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);   // make a test connection
    make_nonblocking(fd);
    int r = connect(fd, (struct sockaddr *)&b->addr, sizeof(b->addr));
    if (r == 0) { backend_set_status(b, BACKEND_HEALTHY); close(fd); return; }
    if (errno != EINPROGRESS) { backend_set_status(b, BACKEND_UNHEALTHY); close(fd); return; }
    // otherwise: the connection is still being made; we'll hear back later
    ...
}
```
This **starts a health test** for one backend. It opens a fresh connection and
tries to `connect`. Three outcomes:
- Connected instantly → healthy, close the test, done.
- Failed immediately (nobody's listening) → unhealthy.
- "Still working on it" (`EINPROGRESS`) → we register this test connection so
  the engine tells us when it finishes. (Networking can take time, so we don't
  wait around — we get notified.)

```c
void health_on_tick(int epfd, backend_pool_t *pool, int timerfd)
{
    uint64_t expirations;
    read(timerfd, &expirations, sizeof(expirations));   // reset the alarm
    for (int i = 0; i < pool->count; i++) {
        backend_t *b = &pool->items[i];
        if (b->probing) {                       // last test never finished?
            cleanup_probe(epfd, b);
            backend_set_status(b, BACKEND_UNHEALTHY);   // too slow = treat as dead
        }
        start_probe(epfd, b);                   // start a fresh test
    }
}
```
This runs **when the alarm rings**. First it reads the timer (which resets the
alarm for next time). Then for each backend: if the *previous* test is somehow
still running, that means the backend is too slow → mark it dead. Either way,
start a new test.

```c
void health_on_probe_event(int epfd, backend_t *b, uint32_t revents)
{
    int healthy = 0;
    if (!(revents & (EPOLLERR | EPOLLHUP))) {
        int err = 0; socklen_t len = sizeof(err);
        if (getsockopt(b->probe_fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0)
            healthy = 1;
    }
    backend_set_status(b, healthy ? BACKEND_HEALTHY : BACKEND_UNHEALTHY);
    cleanup_probe(epfd, b);
}
```
This runs **when a health test finishes**. It asks the connection "did you
actually succeed?" (`getsockopt` with `SO_ERROR` reads the result). If yes →
healthy; if there was any error → unhealthy. Then it cleans up the test
connection.

---

### 4.6 `loop.c` — THE ENGINE (the most important file)

Everything above was setup and helpers. This file is the beating heart: a
loop that never ends, waking up whenever something needs attention.

First, a helper:

```c
int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```
**Non-blocking** is a critical idea. Normally, when you ask to read from a
connection and there's no data yet, your program **freezes** (blocks) until data
arrives. That's a disaster if you're juggling 5,000 connections — freezing on
one freezes all of them.

`make_nonblocking` flips a switch so the connection **never freezes**. If
there's no data, the read instantly says "nothing right now" and we move on.
This function sets that switch. (`fcntl` is the switch-flipping tool; we read
the current settings and add the "don't freeze" option.)

> **Blocking vs non-blocking, plain English:** Blocking = "I'll stand at the
> mailbox until a letter arrives, doing nothing else." Non-blocking = "I'll
> peek at the mailbox; if empty, I go do other things and check again later."

**Setting up the listener** (the front door):

```c
static int setup_listener(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);       // create a socket
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, ...);  // allow quick restart
    bind(fd, ...);                                   // claim port 8080
    listen(fd, SOMAXCONN);                           // start accepting
    make_nonblocking(fd);
    return fd;
}
```
Four steps to open the front door:
1. `socket` — create a network connection point.
2. `setsockopt(SO_REUSEADDR)` — lets us restart the program and reuse port 8080
   immediately (otherwise the OS blocks it for a minute).
3. `bind` — claim port 8080 as ours.
4. `listen` — officially start accepting incoming connections.

**The "what should I listen for" logic** (the trickiest part, so read slowly):

```c
static uint32_t compute_client_mask(const connection_t *c)
{
    if (c->connecting) return 0;              // still dialing kitchen: ignore customer
    uint32_t m = EPOLLRDHUP;
    if (!c->client_eof && buffer_empty(&c->c2b)) m |= EPOLLIN;   // want to READ from customer
    if (buffer_pending(&c->b2c) > 0)             m |= EPOLLOUT;  // want to WRITE to customer
    return m;
}
```
For each connection we tell the system "wake me up when X happens." This
function decides what X is, for the **customer side**:
- `EPOLLIN` = "tell me when the customer sends data" — but only if our
  customer→kitchen bucket is **empty** (has room to receive more). If the bucket
  is full, we deliberately *stop* listening for more, so we don't overflow. This
  is called **backpressure** (politely telling the customer to slow down).
- `EPOLLOUT` = "tell me when I can send to the customer" — only if we have
  kitchen→customer data waiting to go out.

`compute_backend_mask` is the mirror image for the kitchen side. `|=` means
"add this flag to the set."

> **Why bother with this?** If we always listened for everything, the system
> would wake us constantly for things we can't act on — wasting the CPU
> spinning in circles. By listening only for what we can actually handle right
> now, the loop stays efficient and never spins.

**Registering and updating** (telling the system our wishes):

```c
static void conn_register(int epfd, connection_t *c) { ... epoll_ctl(EPOLL_CTL_ADD ...) ... }
static void conn_update  (int epfd, connection_t *c) { ... epoll_ctl(EPOLL_CTL_MOD ...) ... }
```
- `conn_register` = "System, please start watching these two connections, and
  here are their name tags." (`EPOLL_CTL_ADD`)
- `conn_update` = "My needs changed; here's the new list of what to wake me for."
  (`EPOLL_CTL_MOD`) — and it only bothers the system if the needs *actually*
  changed, to avoid pointless work.

**`pump` — the actual byte-copying** (this is what a proxy fundamentally does):

```c
static int pump(int src_fd, int dst_fd, buffer_t *b, int *src_eof)
{
    // STEP 1: if our bucket is empty, try to read from the source
    if (!*src_eof && buffer_empty(b)) {
        ssize_t n = read(src_fd, b->data, BUF_SIZE);
        if (n > 0)      { b->len = n; b->off = 0; }        // got n bytes
        else if (n == 0){ *src_eof = 1; }                  // source hung up
        else if (errno != EAGAIN && ...) return -1;        // real error
        // EAGAIN = "nothing right now" = totally normal, do nothing
    }
    // STEP 2: pour the bucket out toward the destination
    while (buffer_pending(b) > 0) {
        ssize_t n = write(dst_fd, b->data + b->off, buffer_pending(b));
        if (n > 0) { b->off += n; if (b->off == b->len) { b->len = 0; b->off = 0; } }
        else if (n < 0 && (errno == EAGAIN || ...)) break;  // dest is full, try later
        else if (n < 0 && errno == EINTR) continue;
        else return -1;                                      // real error
    }
    // STEP 3: if source is done and bucket is empty, tell the destination
    if (*src_eof && buffer_empty(b)) shutdown(dst_fd, SHUT_WR);
    return 0;
}
```
This moves bytes from one side to the other. Read it as three steps:

**Step 1 — fill the bucket.** If our bucket is empty, `read` from the source.
`read` gives back:
- a positive number = that many bytes arrived → put them in the bucket.
- `0` = the source **hung up** (closed) → remember that (`src_eof = 1`).
- `-1` with `errno == EAGAIN` = "nothing to read right now" → totally normal for
  a non-blocking connection, just carry on.
- `-1` with a different error = something's genuinely broken → return `-1` so
  the caller closes this connection.

**Step 2 — empty the bucket.** `write` the bucket's contents to the destination.
The catch: `write` might only accept **part** of it (the destination can be
temporarily full). So we track how much got out (`off`) and keep going. If the
destination is full right now (`EAGAIN`), we stop and finish later when the
system tells us the destination is ready again.

> **This partial-write detail is the #1 bug in naive proxies.** Beginners assume
> `write` always sends everything. It doesn't. If you ignore the leftover, you
> silently lose data.

**Step 3 — pass along the goodbye.** If the source said goodbye AND we've
forwarded everything, we tell the destination "no more coming from this side"
using `shutdown`. This lets the other end know the conversation half is done,
without abruptly killing everything.

**`conn_close` — cleaning up (with a subtle twist):**

```c
static void conn_close(int epfd, connection_t *c, connection_t **free_head)
{
    if (c->closed) return;              // already closed? do nothing
    c->closed = 1;                      // mark as closed
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->client_fd,  NULL);   // stop watching
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->backend_fd, NULL);
    close(c->client_fd);                // hang up both connections
    close(c->backend_fd);
    if (c->backend && c->backend->active_connections > 0)
        c->backend->active_connections--;   // that kitchen is less busy now
    c->next_free = *free_head;          // add to the "to be deleted" list
    *free_head   = c;
}
```
This ends a conversation: stop watching the two connections, hang them up
(`close`), and note that the kitchen is now less busy.

**The subtle twist:** notice it does **not** immediately delete the memory
(`free`). Why? Because in a single wake-up, the system might report *two*
events for the *same* conversation (one for each side). If we deleted the memory
on the first event, the second event would point at deleted memory — a crash.
So instead we add it to a "delete these later" list (`free_head`) and actually
delete after we've finished handling all events. The `if (c->closed) return;`
at the top makes it safe to try closing the same conversation twice.

**`accept_connections` — greeting new customers:**

```c
static void accept_connections(int epfd, int listen_fd, backend_pool_t *pool)
{
    for (;;) {
        int cfd = accept(listen_fd, NULL, NULL);   // take the next waiting customer
        if (cfd < 0) { if (errno == EAGAIN) break; ... }  // no more waiting
        make_nonblocking(cfd);
        backend_t *b = backend_pick(pool);          // choose a kitchen
        if (!b) { close(cfd); continue; }           // no kitchen available
        int bfd = socket(AF_INET, SOCK_STREAM, 0);  // open a line to the kitchen
        make_nonblocking(bfd);
        int r = connect(bfd, (struct sockaddr *)&b->addr, sizeof(b->addr));
        if (r < 0 && errno != EINPROGRESS) { close(bfd); close(cfd); continue; }
        connection_t *c = connection_create(cfd, bfd, b);
        c->connecting = (r < 0);                    // still dialing the kitchen?
        b->active_connections++;                    // this kitchen just got busier
        conn_register(epfd, c);                     // start watching both sides
    }
}
```
When a new customer knocks, this runs. Step by step:
- `accept` takes the next waiting customer and gives us a new connection number
  (`cfd`). We loop to greet *all* waiting customers, stopping when there are none
  left (`EAGAIN`).
- `backend_pick` chooses which kitchen. If none available, we politely close.
- We open a connection to that kitchen (`bfd`) and start dialing (`connect`).
- Because it's non-blocking, dialing usually returns "in progress"
  (`EINPROGRESS`) — that's expected, not an error.
- We create the conversation record and register both sides to be watched.

**`handle_conn_event` — reacting to a connection:**

```c
static void handle_conn_event(int epfd, connection_t *c, ev_type_t which,
                              uint32_t revents, connection_t **free_head)
{
    if (revents & EPOLLERR) { conn_close(...); return; }   // error? close it

    if (c->connecting) {                     // were we still dialing the kitchen?
        if (which == EV_BACKEND && (revents & (EPOLLOUT | EPOLLHUP))) {
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(c->backend_fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) { conn_close(...); return; }   // dialing failed
            c->connecting = 0;                            // connected!
        } else return;
    }

    if (pump(c->client_fd, c->backend_fd, &c->c2b, &c->client_eof) < 0) { conn_close(...); return; }
    if (pump(c->backend_fd, c->client_fd, &c->b2c, &c->backend_eof) < 0) { conn_close(...); return; }

    if (c->client_eof && c->backend_eof && buffer_empty(&c->c2b) && buffer_empty(&c->b2c)) {
        conn_close(...); return;             // both sides done → close
    }
    conn_update(epfd, c);                    // update what we listen for
}
```
This runs whenever one of our conversations has activity. In order:
1. If the system reports an error, close the conversation.
2. If we were still **dialing the kitchen**, check whether dialing just
   finished. We ask "did it succeed?" (`getsockopt`/`SO_ERROR`). If it failed,
   close; if it worked, mark us connected and continue.
3. **Pump both directions**: customer→kitchen and kitchen→customer. Each pump
   only acts if there's actually something to do, so calling both is safe.
4. If **both** sides have said goodbye and both buckets are empty, the
   conversation is complete → close it.
5. Otherwise, update what we want to be woken for, and wait for the next event.

**`loop_run` — the never-ending loop itself:**

```c
int loop_run(backend_pool_t *pool, int listen_port, int health_interval_sec)
{
    int listen_fd = setup_listener(listen_port);   // open the front door
    int epfd = epoll_create1(0);                   // create the "event watcher"

    // register the front door and the alarm clock with the watcher:
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, ...);   // tag: EV_LISTENER
    int timerfd = health_timer_create(health_interval_sec);
    epoll_ctl(epfd, EPOLL_CTL_ADD, timerfd, ...);     // tag: EV_TIMER

    struct epoll_event events[MAX_EVENTS];
    for (;;) {                                        // <-- the forever loop
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);   // SLEEP until something happens

        connection_t *free_head = NULL;               // "to delete" list for this round
        for (int i = 0; i < n; i++) {                 // handle each thing that happened
            ev_tag_t *tag = events[i].data.ptr;       // read the name tag
            uint32_t revents = events[i].events;
            switch (tag->type) {                      // what kind of event?
            case EV_LISTENER: accept_connections(epfd, listen_fd, pool); break;
            case EV_TIMER:    health_on_tick(epfd, pool, timerfd);       break;
            case EV_PROBE:    health_on_probe_event(epfd, tag->owner, revents); break;
            case EV_CLIENT:
            case EV_BACKEND: {
                connection_t *c = tag->owner;
                if (c->closed) break;                 // ignore already-closed
                handle_conn_event(epfd, c, tag->type, revents, &free_head);
                break;
            }
            }
        }
        while (free_head) {                           // now safely delete finished conversations
            connection_t *next = free_head->next_free;
            free(free_head);
            free_head = next;
        }
    }
}
```
This is the whole program in one loop. Read it as:

1. **Set up once:** open the front door (`setup_listener`), create the event
   watcher (`epoll_create1` — this is the "epoll" system that efficiently
   watches thousands of connections at once), and register the front door and
   the alarm clock with it.

2. **Loop forever** (`for (;;)`):
   - `epoll_wait` = **go to sleep until something happens.** This is the magic:
     while nothing is going on, the program uses **zero CPU** — it's parked. The
     moment *anything* we're watching becomes ready (a new customer, data
     arriving, the alarm ringing), it wakes us and hands back a list of what
     happened, and how many things (`n`).
   - For each thing that happened, we read its **name tag** to learn what kind
     of event it is, then `switch` to the right handler:
     - front door → greet new customers.
     - alarm → run health checks.
     - health test finished → record the result.
     - customer or kitchen activity → pump the bytes.
   - After handling everything, delete any conversations that ended.
   - Go back to sleep.

That's it. **One thread, one loop, reacting to events, handling thousands of
connections** — because it never freezes waiting on any single one.

---

### 4.7 `main.c` — where the program starts

```c
int main(void)
{
    signal(SIGPIPE, SIG_IGN);   // don't let a disconnected customer crash us

    backend_pool_t pool;
    backend_pool_init(&pool, LB_STRATEGY);
    for (int i = 0; i < NUM_BACKENDS; i++)
        backend_pool_add(&pool, BACKENDS[i].host, BACKENDS[i].port);

    return loop_run(&pool, LISTEN_PORT, HEALTH_INTERVAL_SEC);
}
```
The starting point. It:
1. `signal(SIGPIPE, SIG_IGN)` — a safety switch. Normally, if we try to send to
   a customer who already hung up, the operating system **kills our whole
   program**. This line says "ignore that; I'll handle it myself" (we detect it
   as an error from `write` instead).
2. Builds the backend pool from the settings in `config.h`.
3. Calls `loop_run`, which starts the forever loop and **never returns** (until
   you stop the program).

---

## 5. How one request flows through everything

Let's trace a single customer, start to finish, naming the exact functions.
This is the best way to see how the files connect.

```
1. You run ./tcplb
      main()  →  sets up pool  →  loop_run()  →  sleeps in epoll_wait()

2. A customer connects to port 8080
      epoll_wait() wakes, returns an EV_LISTENER event
      → accept_connections()
          → accept()            (greet the customer, get client_fd)
          → backend_pick()      (choose kitchen #2, say)
          → socket() + connect()(start dialing kitchen #2, "in progress")
          → connection_create() (make the conversation record)
          → conn_register()     (ask epoll to watch both sides)
      → back to sleep

3. The dial to kitchen #2 completes
      epoll_wait() wakes with an EV_BACKEND event (backend became writable)
      → handle_conn_event()
          → sees connecting==1, checks it succeeded, sets connecting=0
          → pump() both directions (nothing to send yet)
          → conn_update()  (now listen for the customer's request)
      → back to sleep

4. The customer sends a request
      epoll_wait() wakes with an EV_CLIENT event (customer readable)
      → handle_conn_event()
          → pump(client → backend): read() from customer into c2b bucket,
                                     write() it to kitchen #2
          → conn_update()
      → back to sleep

5. Kitchen #2 sends a reply
      epoll_wait() wakes with an EV_BACKEND event (backend readable)
      → handle_conn_event()
          → pump(backend → client): read() reply into b2c bucket,
                                     write() it to the customer
      → back to sleep

6. Both sides finish and hang up
      read() returns 0 (EOF) on each side  →  client_eof / backend_eof set
      → handle_conn_event() sees both done + buckets empty
          → conn_close()   (stop watching, hang up, mark for deletion)
      → after the batch, free() the conversation record

   Meanwhile, every 3 seconds (independent of all the above):
      epoll_wait() wakes with an EV_TIMER event
      → health_on_tick()  → start_probe() for each backend
      → later, EV_PROBE events → health_on_probe_event() marks UP/DOWN
```

Notice: **everything goes through the one loop.** Customers, kitchens, and the
health clock are all just "events" the loop reacts to, one after another.

---

## 6. The connection state machine

A single connection moves through a small number of **states** over its life.
Memorize this diagram — interviewers love it, and it proves you understand the
whole flow.

```
                    ┌──────────────────────────────────────────────┐
                    │  A new client connects to us on port 8080      │
                    └───────────────────────┬──────────────────────┘
                                             │ accept() + pick backend
                                             │ + start non-blocking connect()
                                             ▼
                                   ┌───────────────────┐
                                   │    CONNECTING     │   connecting = 1
                                   │ (dialing backend) │   (we ignore the client
                                   └─────────┬─────────┘    until the dial finishes)
                          connect failed     │  connect succeeded
                        ┌────────────────────┼────────────────────┐
                        ▼                     ▼ (EPOLLOUT + SO_ERROR==0)
                   ┌─────────┐         ┌───────────────┐
                   │ CLOSED  │         │    ACTIVE     │  connecting = 0
                   │ (error) │         │ bytes flow    │  data pumps both ways:
                   └─────────┘         │ both ways     │   client <-> backend
                                       └───────┬───────┘
                                               │
                     one side hangs up (read() returns 0 = EOF)
                                               │
                                               ▼
                                   ┌───────────────────────┐
                                   │      HALF-CLOSED       │  client_eof OR
                                   │ one direction is done, │  backend_eof = 1
                                   │ the other still flows; │  (we shutdown() the
                                   │ we finish flushing it  │   finished direction)
                                   └───────────┬───────────┘
                                               │
                        both sides done AND both buckets empty
                                               │
                                               ▼
                                        ┌─────────────┐
                                        │   CLOSED     │  closed = 1
                                        │ stop watching│  → fds closed
                                        │ + free memory│  → freed after the batch
                                        └─────────────┘
```

**In words:**
1. **CONNECTING** — we've accepted the client and are dialing the backend. The
   flag `connecting = 1`. We wait for the backend socket to become writable,
   which means the dial finished. If it failed, we go straight to CLOSED.
2. **ACTIVE** — the dial succeeded (`connecting = 0`). Now bytes flow freely in
   both directions via `pump()`.
3. **HALF-CLOSED** — one side hangs up (a `read()` returns 0). We note it
   (`client_eof` or `backend_eof`), finish sending whatever's left in that
   direction, and tell the other side with `shutdown()`. The other direction
   can still be flowing.
4. **CLOSED** — both sides are done and both buckets are empty. We stop
   watching the fds, `close()` them, and `free()` the memory (after the current
   event batch, to stay safe).

---

## 7. Mock interview Q&A sheet

Short, tight answers you can say out loud.

**Q1. What does your project do, in one sentence?**
It's a TCP load balancer: it accepts client connections on one port and
forwards the traffic to one of several backend servers, choosing by
round-robin or least-connections, and it health-checks the backends so dead
ones are removed from rotation.

**Q2. Why is it single-threaded? Isn't that slow?**
It uses an event loop with `epoll`, so one thread can handle thousands of
connections at once. It's fast because it never *blocks* — it never sits idle
waiting on one connection. This avoids the memory and context-switching cost of
one-thread-per-connection.

**Q3. What is `epoll` and why not `select`?**
`epoll` is Linux's mechanism to watch many file descriptors and be told which
ones are ready. Unlike `select`, you register your fds once (not every loop),
the kernel returns only the *ready* ones (not all of them), and there's no
~1024 fd limit. So cost scales with *active* connections, not *total*.

**Q4. What does "non-blocking" mean and why is it required here?**
A non-blocking socket returns immediately instead of freezing when there's no
data (it returns `EAGAIN`). It's required because in a single-threaded loop, a
blocking call on one connection would freeze *all* connections. So every
`accept`, `connect`, `read`, and `write` is non-blocking.

**Q5. How does an event know which connection it belongs to?**
When I register an fd with epoll, I attach a pointer to a small "tag" struct
(`{type, owner}`) via `epoll_event.data.ptr`. When the event fires, epoll hands
that pointer back, so I instantly know the event type and the exact connection
— no lookup or search.

**Q6. What's backpressure and how do you implement it?**
Backpressure is not reading faster than you can write. I only ask epoll to
notify me of "readable" when the outgoing buffer for that direction is *empty*,
and only ask for "writable" when there's pending data. So a slow backend
naturally makes me stop reading from the fast client until things drain.

**Q7. What is a partial write and how do you handle it?**
`write()` may accept fewer bytes than you gave it when the send buffer is full.
I track an offset into the buffer, write what I can, and finish the rest later
when epoll reports the destination is writable (`EPOLLOUT`). Ignoring this is
the classic proxy bug that silently drops data.

**Q8. How does non-blocking `connect` work?**
It returns immediately with `errno == EINPROGRESS`. The connection finishes in
the background. epoll tells me the socket became *writable* when it's done, and
I confirm success with `getsockopt(SO_ERROR)` — zero means connected, non-zero
means the connect failed.

**Q9. How do health checks work without a separate thread?**
I use a `timerfd`, which turns a periodic timer into a file descriptor I can
watch with the same epoll loop. Every few seconds it fires; I open a
non-blocking test connection to each backend. If the connect succeeds, the
backend is healthy; if refused or timed out, it's marked unhealthy and skipped.

**Q10. Round-robin vs least-connections — when do you use each?**
Round-robin just takes turns; it's simple and fair when requests are uniform.
Least-connections sends the next connection to the least busy backend; it's
better when request durations vary, because it avoids overloading a backend
that's still handling slow requests.

**Q11. What happens when a backend dies mid-traffic?**
Existing connections to it will error on read/write and get closed. New
connections won't be routed there because the next health check marks it
unhealthy and `backend_pick` skips unhealthy backends.

**Q12. Why do you ignore `SIGPIPE`?**
Writing to a socket whose peer has closed raises `SIGPIPE`, which by default
kills the process. I ignore it and instead detect the closed peer via `write`
returning `-1` with `errno == EPIPE`, so one rude disconnect can't crash the
whole proxy.

**Q13. Why defer freeing a closed connection?**
In one `epoll_wait` batch, both the client-side and backend-side events for the
same connection can appear. If I freed on the first, the second would use freed
memory. So I mark it closed, close the fds immediately, and free after the whole
batch is processed. A `closed` flag guards against handling the stale event.

**Q14. Is this Layer 4 or Layer 7? What's the difference?**
Layer 4 (TCP) — it forwards raw bytes without understanding them. Layer 7
(e.g. HTTP) would parse the protocol and could route by URL or host. Being L4
makes it protocol-agnostic: it works for HTTP, databases, anything over TCP —
but it can't make content-based decisions.

**Q15. What would you add next?**
Config from a file or CLI flags instead of hardcoding backends; TLS
termination; passive health checks (noticing failures on live traffic, not just
active probes); connection-level metrics/logging; and optionally edge-triggered
epoll to reduce syscalls.

---

### Final tip for the interview
If you can explain three things clearly, you'll do great:
1. **The event loop** — sleep in `epoll_wait`, wake on events, dispatch, repeat.
2. **The name-tag trick** — how an event maps back to its connection instantly.
3. **Non-blocking + backpressure** — why nothing ever freezes, and how buffers
   with offsets keep fast and slow sides in sync.

Everything else is detail built on those three ideas.
```
