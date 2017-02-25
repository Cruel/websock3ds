#include <wslay/wslay.h>

typedef void (*ws3ds_message_callback_type)(const struct wslay_event_on_msg_recv_arg *arg);

struct sockaddr_in create_address(unsigned int address, unsigned short port);
int create_listener(unsigned short port);
void make_socket_nonblock(int fd);

void ws3ds_init(int fd);
int ws3ds_poll();
void ws3ds_exit();
void ws3ds_set_message_callback(ws3ds_message_callback_type callback);

void ws3ds_send_text(const char* text);
void ws3ds_send_binary(const void* data, size_t size);
