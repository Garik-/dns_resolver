/**
 * @author Gar|k <garik.djan@gmail.com>
 * @copyright (c) 2016, http://c0dedgarik.blogspot.ru/
 * @version 1.0
 */

#include "main.h"

static void
ev_ares_io_handler(EV_P_ ev_io * watcher, int revents) {

    options_t * options = (options_t *) (((char *) watcher) - offsetof(options_t, ares.io));

    ares_socket_t rfd = ARES_SOCKET_BAD, wfd = ARES_SOCKET_BAD;

    if (revents & EV_READ)
        rfd = options->ares.io.fd;
    if (revents & EV_WRITE)
        wfd = options->ares.io.fd;

    ares_process_fd(options->ares.channel, rfd, wfd);
}

static void
ev_ares_timeout_handler(struct ev_loop *loop, struct ev_timer *watcher, int events) {

    options_t * options = (options_t *) (((char *) watcher) - offsetof(options_t, ares.tw));

    debug("ev_ares_timeout_handler");

    ev_timer_set(&options->ares.tw, MAXDNSTIME, 0);
    ev_timer_start(options->loop, &options->ares.tw);

    /**
     ares_process.c
     * 
     void ares_process_timeouts(ares_channel channel) {
        struct timeval now = ares__tvnow();
        process_timeouts(channel, &now);
        process_broken_connections(channel, &now);
    }
     */

    ares_process_timeouts(options->ares.channel);

    errno = ETIMEDOUT;
}

static void
ev_ares_sock_state_callback(void *data, int s, int read, int write) {
    options_t * options = (options_t *) data;

    debug("ev_ares_sock_state_callback %d  [%d.%d]", s, read, write);

    //if (ev_is_active(&options->ares.io) && options->ares.io.fd != s) return;

    ev_io_stop(options->loop, &options->ares.io);
    ev_timer_stop(options->loop, &options->ares.tw);


    if (read || write) {
        ev_io_set(&options->ares.io, s, (read ? EV_READ : 0) | (write ? EV_WRITE : 0));
        ev_timer_set(&options->ares.tw, MAXDNSTIME, 0);

        ev_io_start(options->loop, &options->ares.io);
        ev_timer_start(options->loop, &options->ares.tw);
    }
}

static void
ev_ares_dns_callback(void *arg, int status, int timeouts, struct hostent *host) {

    options_t *options = (options_t *) arg;

    if (!host || status != ARES_SUCCESS) {
        debug("- failed to lookup %s\n", ares_strerror(status));
        __sync_fetch_and_add(&options->counters.dnsnotfound, 1);

        return;
    }

    debug("- found address name %s\n", host->h_name);
    __sync_fetch_and_add(&options->counters.dnsfound, 1);
    
    write_out(options,host);
}

int
ev_ares_init_options(options_t *options) {
    options->ares.options.sock_state_cb_data = options;
    options->ares.options.sock_state_cb = ev_ares_sock_state_callback;
    options->ares.options.flags = ARES_FLAG_NOCHECKRESP;

    ev_init(&options->ares.io, ev_ares_io_handler);
    ev_timer_init(&options->ares.tw, ev_ares_timeout_handler, options->timeout, 0);

    return ares_init_options(&options->ares.channel, &options->ares.options, ARES_OPT_SOCK_STATE_CB | ARES_OPT_FLAGS);
}

void
ev_ares_gethostbyname(options_t * options, const char *name) {
    __sync_fetch_and_add(&options->counters.domains, 1);
    ares_gethostbyname(options->ares.channel, name, AF_INET, ev_ares_dns_callback, (void *) options);
}