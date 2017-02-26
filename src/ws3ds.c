#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <nettle/base64.h>
#include <nettle/sha.h>
#include "ws3ds.h"

static wslay_event_context_ptr ws3ds_ctx;
static bool ws3ds_initialized = false;
static struct pollfd ws3ds_event;
static struct Session ws3ds_session;
static ws3ds_message_callback_type ws3ds_message_callback;

struct sockaddr_in create_address(unsigned int address, unsigned short port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = htonl(address);
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    return addr;
}

// Return listening socket, -1 if failed
int create_listener(unsigned short port) {
    struct sockaddr_in addr = create_address(INADDR_ANY, port);
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
    make_socket_nonblock(sock);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
        return -1;
    if (listen(sock, 0) == -1)
        return -1;
    return sock;
}

void make_socket_nonblock(int fd) {
    int status = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, status | O_NONBLOCK);
}

/*
 * Calculates SHA-1 hash of *src*. The size of *src* is *src_length* bytes.
 * *dst* must be at least SHA1_DIGEST_SIZE.
 */
void sha1(uint8_t *dst, const uint8_t *src, size_t src_length)
{
    struct sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, src_length, src);
    sha1_digest(&ctx, SHA1_DIGEST_SIZE, dst);
}

/*
 * Base64-encode *src* and stores it in *dst*.
 * The size of *src* is *src_length*.
 * *dst* must be at least BASE64_ENCODE_RAW_LENGTH(src_length).
 */
void base64(uint8_t *dst, const uint8_t *src, size_t src_length)
{
    struct base64_encode_ctx ctx;
    base64_encode_init(&ctx);
    base64_encode_raw(dst, src_length, src);
}

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*
 * Create Server's accept key in *dst*.
 * *client_key* is the value of |Sec-WebSocket-Key| header field in
 * client's handshake and it must be length of 24.
 * *dst* must be at least BASE64_ENCODE_RAW_LENGTH(20)+1.
 */
void create_accept_key(char *dst, const char *client_key)
{
    uint8_t sha1buf[20], key_src[60];
    memcpy(key_src, client_key, 24);
    memcpy(key_src+24, WS_GUID, 36);
    sha1(sha1buf, key_src, sizeof(key_src));
    base64((uint8_t*)dst, sha1buf, 20);
    dst[BASE64_ENCODE_RAW_LENGTH(20)] = '\0';
}

/* We parse HTTP header lines of the format
 *   \r\nfield_name: value1, value2, ... \r\n
 *
 * If the caller is looking for a specific value, we return a pointer to the
 * start of that value, else we simply return the start of values list.
 */
static char*
http_header_find_field_value(char *header, char *field_name, char *value)
{
    char *header_end,
         *field_start,
         *field_end,
         *next_crlf,
         *value_start;
    int field_name_len;

    /* Pointer to the last character in the header */
    header_end = header + strlen(header) - 1;

    field_name_len = strlen(field_name);

    field_start = header;

    do{
        field_start = strstr(field_start+1, field_name);

        field_end = field_start + field_name_len - 1;

        if(field_start != NULL
            && field_start - header >= 2
            && field_start[-2] == '\r'
            && field_start[-1] == '\n'
            && header_end - field_end >= 1
            && field_end[1] == ':')
        {
            break; /* Found the field */
        }
        else
        {
            continue; /* This is not the one; keep looking. */
        }
    } while(field_start != NULL);

    if(field_start == NULL)
        return NULL;

    /* Find the field terminator */
    next_crlf = strstr(field_start, "\r\n");

    /* A field is expected to end with \r\n */
    if(next_crlf == NULL)
        return NULL; /* Malformed HTTP header! */

    /* If not looking for a value, then return a pointer to the start of values string */
    if(value == NULL)
        return field_end+2;

    value_start = strstr(field_start, value);

    /* Value not found */
    if(value_start == NULL)
        return NULL;

    /* Found the value we're looking for */
    if(value_start > next_crlf)
        return NULL; /* ... but after the CRLF terminator of the field. */

    /* The value we found should be properly delineated from the other tokens */
    if(isalnum(value_start[-1]) || isalnum(value_start[strlen(value)]))
        return NULL;

    return value_start;
}

/*
 * Performs HTTP handshake. *fd* is the file descriptor of the
 * connection to the client. This function returns 0 if it succeeds,
 * or returns -1.
 */
int http_handshake(int fd)
{
    /*
     * Note: The implementation of HTTP handshake in this function is
     * written for just a example of how to use of wslay library and is
     * not meant to be used in production code.  In practice, you need
     * to do more strict verification of the client's handshake.
     */
    char header[16384], accept_key[29], *keyhdstart, *keyhdend, res_header[256];
    size_t header_length = 0, res_header_sent = 0, res_header_length;
    ssize_t r;
    while(1) {
        while((r = read(fd, header+header_length,
                sizeof(header)-header_length)) == -1 && errno == EINTR);
        if(r == -1) {
            perror("read");
            return -1;
        } else if(r == 0) {
            fprintf(stderr, "HTTP Handshake: Got EOF");
            return -1;
        } else {
            header_length += r;
            if(header_length >= 4 && memcmp(header+header_length-4, "\r\n\r\n", 4) == 0) {
                break;
            } else if(header_length == sizeof(header)) {
                fprintf(stderr, "HTTP Handshake: Too large HTTP headers (%u) %s\n", header_length, header);
                return -1;
            }
        }
    }

    if(http_header_find_field_value(header, "Upgrade", "websocket") == NULL ||
            http_header_find_field_value(header, "Connection", "Upgrade") == NULL ||
            (keyhdstart = http_header_find_field_value(header, "Sec-WebSocket-Key", NULL)) == NULL) {
        fprintf(stderr, "HTTP Handshake: Missing required header fields");
        return -1;
    }
    for(; *keyhdstart == ' '; ++keyhdstart);
    keyhdend = keyhdstart;
    for(; *keyhdend != '\r' && *keyhdend != ' '; ++keyhdend);
    if(keyhdend-keyhdstart != 24) {
        printf("%s\n", keyhdstart);
        fprintf(stderr, "HTTP Handshake: Invalid value in Sec-WebSocket-Key");
        return -1;
    }
    create_accept_key(accept_key, keyhdstart);
    snprintf(res_header, sizeof(res_header),
           "HTTP/1.1 101 Switching Protocols\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Accept: %s\r\n"
           "\r\n", accept_key);
    res_header_length = strlen(res_header);
    while(res_header_sent < res_header_length) {
        while((r = write(fd, res_header+res_header_sent,
                 res_header_length-res_header_sent)) == -1 && errno == EINTR);
        if(r == -1) {
            perror("write");
            return -1;
        } else {
            res_header_sent += r;
        }
    }
    return 0;
}

/*
 * This struct is passed as *user_data* in callback function.  The
 * *fd* member is the file descriptor of the connection to the client.
 */
struct Session {
    int fd;
};

ssize_t send_callback(wslay_event_context_ptr ctx,
                      const uint8_t *data, size_t len, int flags,
                      void *user_data)
{
    struct Session *session = (struct Session*)user_data;
    ssize_t r;
    int sflags = 0;
#ifdef MSG_MORE
    if(flags & WSLAY_MSG_MORE) {
        sflags |= MSG_MORE;
    }
#endif // MSG_MORE
    if ((r = send(session->fd, data, len, sflags)) == -1) {
        return -1;
    }
    if(r == -1) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            printf(" wouldblock ");
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            printf(" callback_failure ");
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    }
    return r;
}

ssize_t recv_callback(wslay_event_context_ptr ctx, uint8_t *buf, size_t len,
                      int flags, void *user_data)
{
    struct Session *session = (struct Session*)user_data;
    ssize_t r;
    if ((r = recv(session->fd, buf, len, 0)) == -1) {
        return -1;
    }

    if(r == -1) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            printf(" wouldblock ");
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            printf(" callback_failure ");
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    } else if (r == 0) {
        printf(" callback_failure/disconnected?\n");
        ws3ds_session.fd = -1;
        /* Unexpected EOF is also treated as an error */
        wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        r = -1;
    }
    return r;
}

void on_msg_recv_callback(wslay_event_context_ptr ctx,
                          const struct wslay_event_on_msg_recv_arg *arg,
                          void *user_data)
{
    if (wslay_is_ctrl_frame(arg->opcode)) {
        ws3ds_session.fd = -1;
    } else if (ws3ds_message_callback)
        ws3ds_message_callback(arg);
}

static struct wslay_event_callbacks ws3ds_callbacks = {
    recv_callback,
    send_callback,
    NULL,
    NULL,
    NULL,
    NULL,
    on_msg_recv_callback
};

void ws3ds_init(int fd) {
    ws3ds_session.fd = fd;
    ws3ds_event.fd = fd;
    ws3ds_event.events = POLLIN;
    make_socket_nonblock(fd);
    wslay_event_context_server_init(&ws3ds_ctx, &ws3ds_callbacks, &ws3ds_session);
    ws3ds_initialized = true;
}

int ws3ds_poll() {
    if (!ws3ds_initialized)
        return -2;

    while (wslay_event_want_read(ws3ds_ctx) || wslay_event_want_write(ws3ds_ctx)) {
        // Disconnected from client
        if (ws3ds_session.fd == -1) {
            ws3ds_exit();
            return -1;
        }

        int r = poll(&ws3ds_event, 1, 10);
        if (r == -1) {
            perror("Poll");
            return -1;
        }

        if(((ws3ds_event.revents & POLLIN) && wslay_event_recv(ws3ds_ctx) != 0) ||
          ((ws3ds_event.revents & POLLOUT) && wslay_event_send(ws3ds_ctx) != 0) ||
          (ws3ds_event.revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            printf("Error. Closing connection.\n");
            return -1;
        }

        ws3ds_event.events = 0;
        if(wslay_event_want_read(ws3ds_ctx))
          ws3ds_event.events |= POLLIN;
        if(wslay_event_want_write(ws3ds_ctx))
          ws3ds_event.events |= POLLOUT;

        if (r == 0)
            break;
    }

    return 0;
}

void ws3ds_exit() {
    if (ws3ds_initialized) {
        ws3ds_initialized = false;
        wslay_event_context_free(ws3ds_ctx);
    }
}

void ws3ds_set_message_callback(ws3ds_message_callback_type callback) {
    ws3ds_message_callback = callback;
}

void ws3ds_send_text(const char* text) {
    struct wslay_event_msg msg = {1, text, strlen(text)};
    if (wslay_event_queue_msg(ws3ds_ctx, &msg) != 0)
        printf("ws3ds_send_text failed.\n");
}

void ws3ds_send_binary(const void* data, size_t size) {
    struct wslay_event_msg msg = {2, data, size};
    if (wslay_event_queue_msg(ws3ds_ctx, &msg) != 0)
        printf("ws3ds_send_binary failed.\n");
}
