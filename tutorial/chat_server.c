/*
 * chat_server.c
 * -------------
 * A tiny multi-client chat server built with epoll.
 *
 * What it does: many people can connect at the same time. Whatever one person
 * types is sent to everyone else. That's it -- a group chat.
 *
 * We use ONE thread and epoll to handle everyone at once. This is the same
 * engine the load balancer uses, but much simpler because each client is just
 * one socket (no backend to forward to).
 *
 * Build:  gcc -Wall -o chat_server chat_server.c
 * Run:    ./chat_server
 * Test:   open a few terminals and run:  nc localhost 5000
 *         type in one, watch it appear in the others.
 */

#define _GNU_SOURCE          /* unlocks some Linux-specific features */

#include <stdio.h>           /* printf, perror                        */
#include <stdlib.h>          /* exit                                  */
#include <string.h>          /* memset                                */
#include <unistd.h>          /* close, read, write                    */
#include <errno.h>           /* errno (the "what went wrong" variable) */
#include <fcntl.h>           /* fcntl (to make sockets non-blocking)  */
#include <arpa/inet.h>       /* sockaddr_in, htons, htonl             */
#include <sys/socket.h>      /* socket, bind, listen, accept          */
#include <sys/epoll.h>       /* epoll_create1, epoll_ctl, epoll_wait  */

#define PORT        5000     /* the network "door number" we listen on */
#define MAX_EVENTS  64       /* how many ready sockets epoll hands us at once */
#define BUF_SIZE    1024     /* size of our temporary message buffer  */
#define MAX_CLIENTS 100      /* the most people we'll allow at once    */

/*
 * We keep a simple list of everyone who is connected. Each entry is a
 * "file descriptor" -- just a number the operating system gives us for each
 * connection. client_count is how many are in the list right now.
 */
int clients[MAX_CLIENTS];
int client_count = 0;

/*
 * Make a socket "non-blocking".
 *
 * Normally, asking a socket for data FREEZES the program until data arrives.
 * With one thread and many clients, freezing on one would freeze everyone.
 * Non-blocking means: if there's no data, the call returns immediately with a
 * "nothing right now" signal instead of freezing.
 */
int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);   /* read the socket's current settings */
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);  /* add the "don't freeze" setting */
}

/* Add a newly-connected client's fd to our list. */
void add_client(int fd)
{
    if (client_count < MAX_CLIENTS) {
        clients[client_count] = fd;
        client_count++;
    }
}

/* Remove a client's fd from our list (when they disconnect). */
void remove_client(int fd)
{
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == fd) {
            /* Move the last item into this slot and shrink the list.
             * Order doesn't matter in a chat, so this is the easy way. */
            clients[i] = clients[client_count - 1];
            client_count--;
            return;
        }
    }
}

/*
 * Send a message to everyone EXCEPT the person who sent it.
 * (You don't need to see your own message echoed back.)
 */
void broadcast(int sender_fd, const char *msg, int len)
{
    for (int i = 0; i < client_count; i++) {
        if (clients[i] != sender_fd) {
            write(clients[i], msg, len);
        }
    }
}

int main(void)
{
    /* ---------------------------------------------------------------
     * STEP 1: Create the "listening" socket -- our front door.
     * --------------------------------------------------------------- */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(1);
    }

    /* Let us restart the program and reuse the port immediately. */
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Describe WHERE we want to listen: this machine, on PORT. */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));              /* zero it out first */
    addr.sin_family      = AF_INET;              /* IPv4 */
    addr.sin_addr.s_addr = htonl(INADDR_ANY);    /* accept on any network address */
    addr.sin_port        = htons(PORT);          /* the port, in network byte order */

    /* Claim the port. */
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(1);
    }

    /* Start accepting connections (10 = how many can queue up waiting). */
    if (listen(listen_fd, 10) == -1) {
        perror("listen");
        exit(1);
    }

    make_nonblocking(listen_fd);

    /* ---------------------------------------------------------------
     * STEP 2: Create the epoll instance -- our "event watcher".
     * --------------------------------------------------------------- */
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        exit(1);
    }

    /* Tell epoll: "watch the front door; wake me when someone knocks." */
    struct epoll_event ev;
    ev.events  = EPOLLIN;        /* EPOLLIN = "readable" = something to accept/read */
    ev.data.fd = listen_fd;      /* remember which fd this event is about */
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    /* epoll_wait will fill this array with the sockets that are ready. */
    struct epoll_event events[MAX_EVENTS];
    char buf[BUF_SIZE];          /* temporary space to hold an incoming message */

    printf("Chat server listening on port %d ...\n", PORT);

    /* ---------------------------------------------------------------
     * STEP 3: The event loop -- runs forever.
     * --------------------------------------------------------------- */
    for (;;) {
        /* Sleep here until at least one socket is ready. Returns how many. */
        int num_ready = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (num_ready == -1) {
            if (errno == EINTR)   /* interrupted by a signal: just try again */
                continue;
            perror("epoll_wait");
            break;
        }

        /* Handle each socket that became ready. */
        for (int i = 0; i < num_ready; i++) {
            int fd = events[i].data.fd;   /* the fd we stored earlier comes back */

            /* CASE A: the front door is ready => new people are connecting. */
            if (fd == listen_fd) {
                for (;;) {
                    int client_fd = accept(listen_fd, NULL, NULL);
                    if (client_fd == -1) {
                        /* EAGAIN/EWOULDBLOCK = no more waiting connections. */
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept");
                        break;
                    }
                    make_nonblocking(client_fd);

                    /* Tell epoll to also watch this new client. */
                    struct epoll_event cev;
                    cev.events  = EPOLLIN;
                    cev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev);

                    add_client(client_fd);
                    printf("Client %d connected (%d online)\n", client_fd, client_count);
                }
                continue;   /* done with the front door; go to next ready fd */
            }

            /* CASE B: a client socket is ready => they sent us data. */
            int count = read(fd, buf, BUF_SIZE);

            if (count == 0) {
                /* read == 0 means the client hung up (disconnected). */
                printf("Client %d disconnected (%d online)\n", fd, client_count - 1);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);  /* stop watching it */
                close(fd);                                  /* free the connection */
                remove_client(fd);                          /* remove from our list */
            }
            else if (count < 0) {
                /* read < 0 = an error. EAGAIN just means "no data right now". */
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                /* A real error: drop the client. */
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                remove_client(fd);
            }
            else {
                /* count > 0 = we got 'count' bytes. Send them to everyone else. */
                broadcast(fd, buf, count);
            }
        }
    }

    close(listen_fd);
    close(epfd);
    return 0;
}
