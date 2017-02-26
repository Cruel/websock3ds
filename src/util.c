#include "util.h"

bool readSMDH(u64 titleId, SMDH* smdh) {
    Handle fileHandle;
	static const u32 filePath[] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000};
    u32 archivePath[] = {(u32) (titleId & 0xFFFFFFFF), (u32) ((titleId >> 32) & 0xFFFFFFFF), MEDIATYPE_SD, 0x00000000};
    FS_Path archiveBinPath = {PATH_BINARY, sizeof(archivePath), archivePath};
    FS_Path fileBinPath = {PATH_BINARY, sizeof(filePath), filePath};
    if (R_SUCCEEDED(FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SAVEDATA_AND_CONTENT, archiveBinPath, fileBinPath, FS_OPEN_READ, 0))) {
        u32 bytesRead = 0;
        if (R_SUCCEEDED(FSFILE_Read(fileHandle, &bytesRead, 0, smdh, sizeof(SMDH))) && bytesRead == sizeof(SMDH)) {
            printf(".");
            if (smdh->magic[0] == 'S' && smdh->magic[1] == 'M' && smdh->magic[2] == 'D' && smdh->magic[3] == 'H') {
                FSFILE_Close(fileHandle);
                return true;
            }
        }
        FSFILE_Close(fileHandle);
    }
    return false;
}

// Grabbed from Citra Emulator (citra/src/video_core/utils.h)
static inline u32 morton_interleave(u32 x, u32 y) {
    u32 i = (x & 7) | ((y & 7) << 8); // ---- -210
    i = (i ^ (i << 2)) & 0x1313;      // ---2 --10
    i = (i ^ (i << 1)) & 0x1515;      // ---2 -1-0
    i = (i | (	i >> 7)) & 0x3F;
    return i;
}

//Grabbed from Citra Emulator (citra/src/video_core/utils.h)
static inline u32 get_morton_offset(u32 x, u32 y) {
    u32 i = morton_interleave(x, y);
    unsigned int offset = (x & ~7) * 8;
    return (i + offset);
}

void untileIcon(void* data) {
    static const u16 w = 48, h = 48;
    u16* src = data;
    u16* dst = malloc(w*h*2);

    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            u32 coarse_y = j & ~7;
            u32 src_offset = get_morton_offset(i, j) + coarse_y * w;
            dst[i+j*w] = *(src + src_offset);
        }
    }

    memcpy(data, dst, w*h*2);
    free(dst);
}
