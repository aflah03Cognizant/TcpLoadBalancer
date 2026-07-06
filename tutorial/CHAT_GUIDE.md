# Learn epoll by building a chat server — every single line explained

This is a gentle, complete walkthrough of `chat_server.c`. We explain **every
line and every function**, assuming you're new to programming. Read it top to
bottom. By the end you'll understand `epoll`, and the load balancer will make
sense too.

---

## Part 0: The mental picture

We're building a **group chat**. Many people connect to our program at the same
time. When one person types a message, we send it to everyone else.

The hard part isn't the chat — it's handling **many people at once with one
program**. That's what `epoll` solves. We'll get there step by step.

### What is a "socket"?

A **socket** is one end of a network connection. Think of it as a telephone
handset. When two programs talk over the network, each holds a socket (a
handset), and they speak through them.

### What is a "file descriptor" (fd)?

When you create a socket (or open a file), the operating system doesn't hand you
the socket itself — it hands you a **number** that represents it. That number is
a **file descriptor**, or **fd**.

Analogy: at a coat check, you don't carry your coat around — you get a **ticket
number**. Later you say "give me coat #7." The fd is that ticket number. Every
read, write, or close uses the fd to say *which* connection you mean.

So in our chat, each connected person is really just an **fd** (a number) we
hold onto.

---

## Part 1: The setup lines

### Line: `#define _GNU_SOURCE`
```c
#define _GNU_SOURCE
```
This must be the **very first line**. It's a switch that unlocks some
Linux-specific features in the headers below. You won't interact with it
directly — just always put it first on Linux.

### The `#include` lines
```c
#include <stdio.h>       // printf, perror
#include <stdlib.h>      // exit
#include <string.h>      // memset
#include <unistd.h>      // close, read, write
#include <errno.h>       // errno
#include <fcntl.h>       // fcntl
#include <arpa/inet.h>   // sockaddr_in, htons, htonl
#include <sys/socket.h>  // socket, bind, listen, accept
#include <sys/epoll.h>   // epoll_create1, epoll_ctl, epoll_wait
```
`#include` means **"paste the contents of this file here."** Each of these is a
system header — a menu of functions the operating system provides. We include
them so we're allowed to call `printf`, `socket`, `epoll_wait`, and so on. The
comment after each line says which functions come from it.

> **Why include them?** C doesn't know about `printf` or `socket` by default.
> The header *announces* those functions exist so the compiler lets you call
> them. (This is the ".h announces things" idea from before.)

### The `#define` settings
```c
#define PORT        5000
#define MAX_EVENTS  64
#define BUF_SIZE    1024
#define MAX_CLIENTS 100
```
`#define NAME value` means "wherever you see NAME, paste value instead." These
are just named settings:
- `PORT 5000` — the door number we listen on.
- `MAX_EVENTS 64` — the most "ready sockets" epoll will report to us in one go.
- `BUF_SIZE 1024` — the size (in bytes) of our temporary message-holding box.
- `MAX_CLIENTS 100` — the biggest chat we allow.

Using names instead of raw numbers makes the code readable and easy to change.

---

## Part 2: The global client list

```c
int clients[MAX_CLIENTS];
int client_count = 0;
```
- `int clients[MAX_CLIENTS];` — an **array** (a numbered row of boxes) that can
  hold up to 100 integers. Each integer is one connected person's fd. So
  `clients` is "the list of everyone in the chat."
- `int client_count = 0;` — how many people are currently in the list. Starts at
  0 (nobody connected yet).

These are **global** variables (declared outside any function), so every
function in the file can use them. That's fine for a small program.

---

## Part 3: The helper functions

We wrote four small helpers so `main` stays readable. Let's go through each.

### `make_nonblocking`
```c
int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```
**What it does:** flips a socket into "non-blocking" mode so it never freezes.

Line by line:
- `int make_nonblocking(int fd)` — a function that takes one fd (a socket
  number) and returns an int (0 for success, -1 for failure).
- `int flags = fcntl(fd, F_GETFL, 0);` — `fcntl` is a tool for reading/changing
  a file descriptor's settings. `F_GETFL` means "**get** the current flags." We
  save them in `flags`.
- `if (flags == -1) return -1;` — if reading the flags failed, give up.
- `return fcntl(fd, F_SETFL, flags | O_NONBLOCK);` — now **set** the flags
  (`F_SETFL`) to the old flags **plus** the "non-blocking" option
  (`O_NONBLOCK`). The `|` symbol means "combine these" (keep the old settings,
  add this one).

> **Why "non-blocking" matters:** Imagine one program serving 100 people. If
> asking person #1 "did you type anything?" makes the program *wait* until they
> do, the other 99 are stuck. Non-blocking means "if nothing's there, tell me
> immediately and I'll move on." That "nothing's there" signal is called
> `EAGAIN` — you'll see it later.

### `add_client`
```c
void add_client(int fd)
{
    if (client_count < MAX_CLIENTS) {
        clients[client_count] = fd;
        client_count++;
    }
}
```
**What it does:** adds a new person's fd to our list.
- `void` means this function returns nothing.
- `if (client_count < MAX_CLIENTS)` — only add if there's room (don't overflow
  the array).
- `clients[client_count] = fd;` — put the fd in the next empty slot. (If
  `client_count` is 3, we fill `clients[3]`.)
- `client_count++;` — `++` means "add 1." Now the list is one longer.

### `remove_client`
```c
void remove_client(int fd)
{
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == fd) {
            clients[i] = clients[client_count - 1];
            client_count--;
            return;
        }
    }
}
```
**What it does:** removes a person's fd from the list when they leave.
- `for (int i = 0; i < client_count; i++)` — a **loop** that walks through every
  slot in the list. `i` starts at 0 and goes up by 1 each turn until it reaches
  `client_count`. (Arrays start counting at 0.)
- `if (clients[i] == fd)` — did we find the fd we're looking for? (`==` means
  "is equal to"; a single `=` would mean "assign," a common beginner mistake.)
- `clients[i] = clients[client_count - 1];` — the trick: copy the **last**
  person into this slot. Since chat order doesn't matter, this is a fast way to
  remove without shifting everyone down.
- `client_count--;` — shrink the list by 1 (the last slot is now a duplicate we
  ignore).
- `return;` — we found it, so stop looping.

### `broadcast`
```c
void broadcast(int sender_fd, const char *msg, int len)
{
    for (int i = 0; i < client_count; i++) {
        if (clients[i] != sender_fd) {
            write(clients[i], msg, len);
        }
    }
}
```
**What it does:** sends a message to everyone except the sender.
- `int sender_fd` — who sent the message (so we can skip them).
- `const char *msg` — the message. `char *` means "a pointer to some
  characters" — basically "the text." `const` means "we promise not to change
  it."
- `int len` — how many bytes the message is.
- The loop walks every client. `if (clients[i] != sender_fd)` skips the sender
  (`!=` means "not equal to").
- `write(clients[i], msg, len);` — **send** `len` bytes of `msg` to that
  client's socket. `write` is how you send data over a socket.

---

## Part 4: `main` — the program itself

`main` is where execution starts. It has three big steps: set up the front door,
set up epoll, then loop forever.

### STEP 1: Create the listening socket

```c
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
if (listen_fd == -1) {
    perror("socket");
    exit(1);
}
```
- `socket(AF_INET, SOCK_STREAM, 0)` — create a new socket.
  - `AF_INET` = use IPv4 (normal internet addresses like 127.0.0.1).
  - `SOCK_STREAM` = use TCP (a reliable, ordered stream of bytes — what chat and
    web use).
  - `0` = default protocol.
  - It returns an fd (a number) for our new socket, saved in `listen_fd`.
- `if (listen_fd == -1)` — sockets return -1 on failure. If so:
  - `perror("socket")` — print a human-readable error message (e.g. "socket:
    Too many open files").
  - `exit(1)` — quit the program with an error code (1 = "something went
    wrong").

```c
int opt = 1;
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```
- `setsockopt(...)` sets an option on the socket. `SO_REUSEADDR` lets us restart
  the program and immediately reuse port 5000. Without it, the OS keeps the port
  reserved for a minute or two after you stop, and `bind` fails with "Address
  already in use." `&opt` is "the address of opt" (setsockopt wants a pointer);
  `sizeof(opt)` tells it how big that value is.

```c
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);
addr.sin_port        = htons(PORT);
```
This block describes **where** we want to listen.
- `struct sockaddr_in addr;` — a struct (a bundle of fields) that holds an IPv4
  address + port.
- `memset(&addr, 0, sizeof(addr));` — fill the whole struct with zeros first, so
  no leftover garbage remains. (`memset` = "set memory": address, value, size.)
- `addr.sin_family = AF_INET;` — again, IPv4.
- `addr.sin_addr.s_addr = htonl(INADDR_ANY);` — `INADDR_ANY` means "listen on all
  of this machine's network addresses." `htonl` converts the number to **network
  byte order** (explained below).
- `addr.sin_port = htons(PORT);` — set the port (5000), also converted to network
  byte order with `htons`.

> **Network byte order (`htons`, `htonl`):** Different computers store numbers in
> different byte arrangements ("endianness"). The network agreed on one standard
> order. `htons` = "host to network, short (16-bit)" — used for the port.
> `htonl` = "host to network, long (32-bit)" — used for the address. These just
> make sure the number is arranged the way the network expects. **This is a
> classic interview question.**

```c
if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    exit(1);
}
```
- `bind(...)` — **claim** the port. It attaches our address/port to the socket.
- `(struct sockaddr *)&addr` — a **cast**. `bind` wants a generic address type,
  but we have the specific IPv4 type. The cast says "treat my IPv4 address as the
  generic kind." (Common C pattern; don't overthink it.)
- If it returns -1, the port is taken or forbidden → print error and quit.

```c
if (listen(listen_fd, 10) == -1) {
    perror("listen");
    exit(1);
}
```
- `listen(...)` — flip the socket into "accepting connections" mode. The `10` is
  the **backlog**: how many connections can wait in line before we `accept` them.

```c
make_nonblocking(listen_fd);
```
Make the listening socket non-blocking too, so `accept` won't freeze us.

At this point our **front door is open** on port 5000.

### STEP 2: Create the epoll instance

```c
int epfd = epoll_create1(0);
if (epfd == -1) {
    perror("epoll_create1");
    exit(1);
}
```
- `epoll_create1(0)` — create an **epoll instance**. Think of epoll as a smart
  assistant who can watch hundreds of sockets for you and tap you on the shoulder
  only when one has something to do. It returns an fd (`epfd`) that represents
  this assistant. `0` means "no special options."

```c
struct epoll_event ev;
ev.events  = EPOLLIN;
ev.data.fd = listen_fd;
epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
```
Now we tell the assistant what to watch.
- `struct epoll_event ev;` — a little form we fill in to describe a request.
- `ev.events = EPOLLIN;` — "wake me when this socket is **readable**." For the
  listening socket, "readable" means "a new client is knocking."
- `ev.data.fd = listen_fd;` — this is important: epoll lets us attach a piece of
  info to each watched socket, and it hands that info back to us later. We store
  the fd itself, so when the event fires we know which socket it was.
- `epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);` — the "control" function:
  - `epfd` = our assistant.
  - `EPOLL_CTL_ADD` = "add this to your watch list."
  - `listen_fd` = the socket to watch.
  - `&ev` = the request form we filled in.

```c
struct epoll_event events[MAX_EVENTS];
char buf[BUF_SIZE];
```
- `events[MAX_EVENTS]` — an array epoll will fill with "here are the sockets that
  are ready right now" (up to 64 at a time).
- `buf[BUF_SIZE]` — a 1024-byte scratch box to hold an incoming message.

```c
printf("Chat server listening on port %d ...\n", PORT);
```
- `printf` prints text. `%d` is a placeholder that gets replaced by `PORT`
  (5000). `\n` means "new line." So it prints: `Chat server listening on port
  5000 ...`

### STEP 3: The event loop (the heart)

```c
for (;;) {
```
- `for (;;)` is an **infinite loop** — it repeats forever. (It has no
  stop-condition, so it never ends on its own.) This is the engine.

```c
int num_ready = epoll_wait(epfd, events, MAX_EVENTS, -1);
```
**This is the single most important line.**
- `epoll_wait(...)` — "Assistant, put me to sleep and only wake me when one or
  more watched sockets is ready. Then tell me which ones."
  - `epfd` = our assistant.
  - `events` = the array it fills with the ready sockets.
  - `MAX_EVENTS` = the array's size (don't give me more than 64 at once).
  - `-1` = the timeout: "-1 means wait forever." (You could pass milliseconds
    to wake up periodically instead.)
- It returns `num_ready` = how many sockets are ready.

> **Why this is magic:** while nothing is happening, the program **sleeps and
> uses zero CPU.** The operating system wakes it the instant *anything* it's
> watching becomes ready. So one program can watch 5,000 sockets and only do
> work when there's actually work to do.

```c
if (num_ready == -1) {
    if (errno == EINTR)
        continue;
    perror("epoll_wait");
    break;
}
```
- If `epoll_wait` returns -1, something went wrong.
- `errno == EINTR` means "a signal interrupted the wait" — harmless; `continue`
  jumps back to the top of the loop and waits again.
- Otherwise it's a real error: print it and `break` out of the loop (ending the
  program).

```c
for (int i = 0; i < num_ready; i++) {
    int fd = events[i].data.fd;
```
- Loop over each ready socket (there are `num_ready` of them).
- `int fd = events[i].data.fd;` — remember we stored the fd in `ev.data.fd`
  earlier? Now epoll hands it back. So `fd` is "the socket that's ready."

Now we split into two cases: is it the front door, or an existing client?

#### CASE A: the front door (new connections)

```c
if (fd == listen_fd) {
    for (;;) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            perror("accept");
            break;
        }
        make_nonblocking(client_fd);

        struct epoll_event cev;
        cev.events  = EPOLLIN;
        cev.data.fd = client_fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev);

        add_client(client_fd);
        printf("Client %d connected (%d online)\n", client_fd, client_count);
    }
    continue;
}
```
- `if (fd == listen_fd)` — the ready socket is our front door, meaning one or
  more people are connecting.
- `for (;;)` — loop to accept **all** waiting connections (there might be
  several).
- `accept(listen_fd, NULL, NULL)` — take the next waiting connection and get a
  **new fd** for that specific client (`client_fd`). The two `NULL`s mean "I
  don't care to record the client's address."
- `if (client_fd == -1)` — accept failed.
  - `EAGAIN`/`EWOULDBLOCK` here means "no more connections waiting" → `break`
    out of the accept loop (normal).
  - otherwise print the error and break.
- `make_nonblocking(client_fd);` — the new client socket must also be
  non-blocking.
- The `struct epoll_event cev; ...; epoll_ctl(... ADD ...)` block — tell epoll to
  **also watch this new client** for readable data, storing its fd so we get it
  back later. Same pattern as the listening socket.
- `add_client(client_fd);` — add them to our chat list.
- `printf(...)` — log it, e.g. `Client 6 connected (2 online)`.
- `continue;` — we're done with the front door; skip to the next ready socket.

#### CASE B: an existing client sent data

```c
int count = read(fd, buf, BUF_SIZE);
```
- `read(fd, buf, BUF_SIZE)` — read up to 1024 bytes from this client into `buf`.
- It returns `count`:
  - **positive** = that many bytes arrived.
  - **0** = the client hung up (disconnected).
  - **-1** = an error (or just "nothing right now").

```c
if (count == 0) {
    printf("Client %d disconnected (%d online)\n", fd, client_count - 1);
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    remove_client(fd);
}
```
- `count == 0` → the client left.
- `epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);` — tell epoll "stop watching this
  socket" (`EPOLL_CTL_DEL` = delete from the watch list).
- `close(fd);` — hang up the connection and free the fd.
- `remove_client(fd);` — take them out of our chat list.

```c
else if (count < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        continue;
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
    remove_client(fd);
}
```
- `count < 0` → error.
- `EAGAIN`/`EWOULDBLOCK` = "no data right now, false alarm" → `continue` (do
  nothing, move on).
- Any other error → drop the client (same cleanup as disconnect).

```c
else {
    broadcast(fd, buf, count);
}
```
- `count > 0` → we got a real message of `count` bytes. Send it to everyone else
  with `broadcast`.

That's the whole loop. After handling all ready sockets, it goes back to
`epoll_wait` and sleeps again.

```c
close(listen_fd);
close(epfd);
return 0;
```
These run only if the loop breaks (an error). They close the front door and the
epoll assistant, then `return 0` (success) from `main`.

---

## Part 5: epoll, explained simply (the big picture)

Now that you've seen it used, here's the concept in plain words.

**The problem:** one program, many connections. You can't just `read` from each
one in turn, because `read` would freeze on whichever has no data yet.

**The old solution (`select`):** ask the OS "out of ALL my sockets, which are
ready?" — every single loop. This is slow because you re-hand the OS the whole
list every time, and it re-scans all of them.

**The epoll solution — three steps:**

1. **Create** the watcher once: `epoll_create1`.
2. **Register** each socket once: `epoll_ctl(... EPOLL_CTL_ADD ...)`. You say "watch
   this fd, and here's a note (`data.fd`) to hand me back when it fires."
3. **Wait** in a loop: `epoll_wait`. It sleeps until sockets are ready, then hands
   you back **only the ready ones** — not the whole list.

So instead of re-asking about everything constantly, you set it up once and then
just receive "here's what's ready" notifications. That's why epoll scales to
thousands of connections cheaply.

**The three epoll functions, summarized:**

| Function | Plain meaning |
|----------|---------------|
| `epoll_create1(0)` | "Hire an assistant to watch sockets." Returns `epfd`. |
| `epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)` | "Assistant, watch this socket." (Also `_MOD` to change, `_DEL` to stop.) |
| `epoll_wait(epfd, events, MAX, -1)` | "Sleep until something's ready, then tell me which." |

**`EPOLLIN`** = "notify me when this socket is **readable**" (has data to read, or
a new connection to accept). There's also `EPOLLOUT` (writable), which the load
balancer uses but this chat doesn't need.

---

## Part 6: Build and try it

On Linux (or WSL on Windows):

```sh
cd tutorial
gcc -Wall -o chat_server chat_server.c
./chat_server
```

Then open **two or three more terminals** and in each run:
```sh
nc localhost 5000        # 'nc' = netcat, a tool that connects to a port
```
Type a message in one terminal and press Enter — it appears in the others. That's
your one-threaded, epoll-powered chat server working.

(If `nc` isn't installed: `sudo apt install netcat`.)

---

## Part 7: From chat server to load balancer

Here's the payoff. The load balancer is the **exact same engine** with two
differences:

| | Chat server | Load balancer |
|---|---|---|
| When a client sends data | broadcast to all other clients | forward to **one backend** |
| Each client tracked as | 1 socket (fd) | 2 sockets: client **and** its backend |
| Extra features | none | pick a backend, health-check backends |

So the load balancer keeps the same `epoll_create1` → `epoll_ctl` → `epoll_wait`
loop. It just:
1. On a new client, also opens a **second** socket to a backend server.
2. Instead of broadcasting, it copies bytes between the client and its backend
   (that's the `pump` function).
3. Adds a timer (also watched by epoll) to health-check backends.

Everything you learned here — sockets, fds, non-blocking, `EAGAIN`, the epoll
loop, `accept`/`read`/`write`/`close` — is *identical* in the load balancer.
The only new idea there is "each connection has two sockets instead of one, and
we shuffle bytes between them."

Read this guide until the chat server feels obvious, then re-read the load
balancer code. It'll click.
