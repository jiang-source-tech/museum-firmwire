#ifndef TEST_STUB_LVGL_H
#define TEST_STUB_LVGL_H

#include <stdint.h>
#include <stdlib.h>

#define LV_GIF_CACHE_DECODE_DATA 0
#define LV_USE_DRAW_SW_ASM 0
#define LV_DRAW_SW_ASM_HELIUM 1

#define LV_FS_MODE_RD 0
#define LV_FS_SEEK_SET 0
#define LV_FS_SEEK_CUR 1
#define LV_FS_RES_OK 0

typedef int lv_fs_file_t;
typedef int lv_fs_res_t;

static inline void* lv_malloc(size_t size) {
    return malloc(size);
}

static inline void lv_free(void* ptr) {
    free(ptr);
}

static inline void* lv_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

static inline lv_fs_res_t lv_fs_open(lv_fs_file_t* fd, const char* path, int mode) {
    (void)fd;
    (void)path;
    (void)mode;
    return -1;
}

static inline lv_fs_res_t lv_fs_read(lv_fs_file_t* fd, void* buf, size_t len, uint32_t* read_num) {
    (void)fd;
    (void)buf;
    if (read_num != NULL) {
        *read_num = (uint32_t)len;
    }
    return LV_FS_RES_OK;
}

static inline lv_fs_res_t lv_fs_seek(lv_fs_file_t* fd, uint32_t pos, int whence) {
    (void)fd;
    (void)pos;
    (void)whence;
    return LV_FS_RES_OK;
}

static inline lv_fs_res_t lv_fs_tell(lv_fs_file_t* fd, uint32_t* pos) {
    (void)fd;
    if (pos != NULL) {
        *pos = 0;
    }
    return LV_FS_RES_OK;
}

static inline lv_fs_res_t lv_fs_close(lv_fs_file_t* fd) {
    (void)fd;
    return LV_FS_RES_OK;
}

#endif
