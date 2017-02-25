#include <3ds.h>
#include <stdio.h>
#include "ws3ds.h"

#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define PORT 5050
#define SOC_BUFFERSIZE 0x100000

static u32* socBuffer;

bool service_init() {
    gfxInit(GSP_RGBA8_OES, GSP_BGR8_OES, false);
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_BOTTOM, NULL);
    socBuffer = (u32*)memalign(0x1000, SOC_BUFFERSIZE);
    return R_SUCCEEDED(socInit(socBuffer, SOC_BUFFERSIZE));
}

void service_exit() {
    ws3ds_exit();
    socExit();
    free(socBuffer);
    gfxExit();
}

void on_message(const struct wslay_event_on_msg_recv_arg *arg) {
    if (arg->opcode == 1) { // Text type
        char s[arg->msg_length+1];
        memcpy(s, arg->msg, arg->msg_length);
        s[arg->msg_length] = '\0';
        printf("Text received: %s\n", s);
    } else if (arg->opcode == 2) { // Binary type
        u32 size = 400 * 240 * 4;
        if (arg->msg_length == size) {
            printf("Image received.\n");
            u8* dst = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
            memcpy(dst, arg->msg, size);
        }
    }
}

int main(int argc, char **argv)
{
    atexit(service_exit);
    if (!service_init())
        return EXIT_FAILURE;

    bool connected = false;
    bool connecting = false;
    int socket_client = -1;
    int socket_server = create_listener(PORT);

    // If TCP listener failed, just give up on life...
    if (socket_server == -1)
        return EXIT_FAILURE;

    struct in_addr addr = {(in_addr_t) gethostid()};
    printf("IP is %s\n", inet_ntoa(addr));
    printf("Press SELECT to exit.\n");

    ws3ds_set_message_callback(on_message);

    while (aptMainLoop()) {
        // Connect to client if not already connected
        if (!connected && !connecting) {
            connecting = true;
            printf("Waiting for client connection...\n");
        }
        if (connecting) {
            struct sockaddr_in addr;
            socklen_t addrSize = sizeof(addr);

            if (socket_client == -1)
                socket_client = accept(socket_server, &addr, &addrSize);
            if (socket_client != -1) {
                if (http_handshake(socket_client) == -1) {
                    printf("Websocket handshake failed!\n");
                    close(socket_client);
                    socket_client = -1;
                } else {
                    connecting = false;
                    printf("Client connected (%s).", inet_ntoa(addr.sin_addr));
                    ws3ds_init(socket_client);
                    connected = true;
                }
            }
        }

        gfxFlushBuffers();
        gspWaitForVBlank();
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_SELECT)
            break;

        // If client (web browser) is connected
        if (connected) {
            if (kDown & KEY_A) {
                ws3ds_send_text("meow!");
            }

            // Poll events, -1 indicates connection loss
            if (ws3ds_poll() == -1) {
                connected = false;
                close(socket_client);
                socket_client = -1;
            }
        }
    }

    return EXIT_SUCCESS;
}
