#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <malloc.h>

#include <3ds.h>
#include <jansson.h>
#include <nettle/base64.h>
#include "ws3ds.h"
#include "util.h"

#define VERSION "1.0"
#define PORT 5050
#define SOC_BUFFERSIZE 0x100000

static u32* socBuffer;

bool service_init() {
    gfxInit(GSP_RGBA8_OES, GSP_BGR8_OES, false);
    gfxSetDoubleBuffering(GFX_TOP, false);
    consoleInit(GFX_BOTTOM, NULL);
    amInit();
    cfguInit();
    socBuffer = (u32*)memalign(0x1000, SOC_BUFFERSIZE);
    return R_SUCCEEDED(socInit(socBuffer, SOC_BUFFERSIZE));
}

void service_exit() {
    ws3ds_exit();
    socExit();
    cfguExit();
    amExit();
    free(socBuffer);
    gfxExit();
}

// Build JSON string of app list and send it to client
void send_app_list() {
    u32 titleCount, i;
    char strTitleId[20];
    json_t *node, *root = json_array();
    SMDH smdh;

    CFG_Language systemLanguage;
    CFGU_GetSystemLanguage((u8*)&systemLanguage);

    AM_GetTitleCount(MEDIATYPE_SD, &titleCount);
    u64* titleIds = malloc(titleCount * sizeof(u64));
    AM_GetTitleList(NULL, MEDIATYPE_SD, titleCount, titleIds);

    u32 iconBase64Size = BASE64_ENCODE_RAW_LENGTH(sizeof(smdh.largeIcon));
    char* iconBase64 = malloc(iconBase64Size+1);
    iconBase64[iconBase64Size] = '\0';

    printf("Sending title info");

    for (i = 0; i < titleCount; ++i) {
        node = json_array();
        u64 titleId = *(titleIds + i);
        if (readSMDH(titleId, &smdh)) {
            char shortDescription[0x100] = {'\0'};
            char longDescription[0x200] = {'\0'};
            sprintf(strTitleId, "0x%016llX", titleId);
            untileIcon(smdh.largeIcon);
            base64((u8*)iconBase64, smdh.largeIcon, sizeof(smdh.largeIcon));
            utf16_to_utf8((uint8_t*)shortDescription, smdh.titles[systemLanguage].shortDescription, sizeof(shortDescription) - 1);
            utf16_to_utf8((uint8_t*)longDescription, smdh.titles[systemLanguage].longDescription, sizeof(longDescription) - 1);
            json_array_append(node, json_string(strTitleId));
            json_array_append(node, json_string(shortDescription));
            json_array_append(node, json_string(longDescription));
            json_array_append(node, json_string(iconBase64));
            json_array_append(root, node);
        }
    }

    printf(" done!\n");

    char* outstr = json_dumps(root, 0);
    ws3ds_send_text(outstr);
    free(outstr);
    free(iconBase64);
    free(titleIds);
}

void on_message(const struct wslay_event_on_msg_recv_arg *arg) {
    if (arg->opcode == 1) { // Text type
        // Note: msg is not NULL terminated
        if (arg->msg_length == 8 && strncmp("LISTAPPS", arg->msg, 8) == 0)
            send_app_list();
        else
            printf("Text received: %.*s\n", arg->msg_length, arg->msg);
    } else if (arg->opcode == 2) { // Binary type
        u32 size = 400 * 240 * 4;
        if (arg->msg_length == size) {
            printf("Image received.\n");
            // Copy pixels directly to framebuffer
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
    printf("Websocket3ds " VERSION "\n");
    printf("Console IP is %s\n", inet_ntoa(addr));
    printf("Press SELECT to exit.\n\n");

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
                socket_client = accept(socket_server, (struct sockaddr*)&addr, &addrSize);
            if (socket_client != -1) {
                if (http_handshake(socket_client) == -1) {
                    printf("Websocket handshake failed!\n");
                    close(socket_client);
                    socket_client = -1;
                } else {
                    connecting = false;
                    connected = true;
                    printf("Client connected (%s).\n", inet_ntoa(addr.sin_addr));
                    ws3ds_init(socket_client);
                    ws3ds_send_text("VERSION " VERSION);
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
