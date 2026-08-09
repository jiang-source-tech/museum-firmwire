#include "gifdec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char* path;
    uint16_t width;
    uint16_t height;
} gif_probe_case_t;

static const gif_probe_case_t k_gif_cases[] = {
    {"main/assets/images/idle.gif", 256, 256},
    {"main/assets/images/working.gif", 256, 256},
    {"main/assets/images/speaking_fixed.gif", 192, 208},
    {"main/assets/images/thinking.gif", 256, 256},
    {"main/assets/images/waiting.gif", 256, 256},
    {"main/assets/images/done.gif", 256, 256},
    {"main/assets/images/sleeping.gif", 256, 256},
    {"main/assets/images/jumping.gif", 256, 256},
    {"main/assets/images/failed.gif", 256, 256},
    {"main/assets/images/giddy.gif", 256, 256},
    {"main/assets/images/review.gif", 256, 256},
    {"main/assets/images/happy.gif", 256, 256},
    {"main/assets/images/crying.gif", 256, 256},
    {"main/assets/images/anxiety.gif", 256, 256},
    {"main/assets/images/tired.gif", 256, 256},
    {"main/assets/images/stamp.gif", 256, 256},
};

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    const long length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);

    uint8_t* data = (uint8_t*)malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }

    const size_t read_count = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length) {
        free(data);
        return NULL;
    }

    *size = (size_t)length;
    return data;
}

static int decode_gif_file(const gif_probe_case_t* probe_case) {
    const char* path = probe_case->path;
    size_t size = 0;
    uint8_t* data = read_file(path, &size);
    if (data == NULL) {
        fprintf(stderr, "failed to read GIF file: %s\n", path);
        return 1;
    }

    gd_GIF* gif = gd_open_gif_data(data);
    if (gif == NULL) {
        fprintf(stderr, "failed to open GIF data: %s\n", path);
        free(data);
        return 1;
    }

    if (gif->width != probe_case->width || gif->height != probe_case->height) {
        fprintf(stderr, "unexpected GIF size for %s: %ux%u\n", path, gif->width, gif->height);
        gd_close_gif(gif);
        free(data);
        return 1;
    }

    gd_render_frame(gif, gif->canvas);
    int frames = 1;
    for (; frames < 6; frames++) {
        if (gd_get_frame(gif) == 0) {
            break;
        }
        gd_render_frame(gif, gif->canvas);
    }

    gd_close_gif(gif);
    free(data);

    if (frames < 6) {
        fprintf(stderr, "decoded only %d frames: %s\n", frames, path);
        return 1;
    }

    return 0;
}

int main(void) {
    for (size_t i = 0; i < sizeof(k_gif_cases) / sizeof(k_gif_cases[0]); i++) {
        if (decode_gif_file(&k_gif_cases[i]) != 0) {
            return 1;
        }
    }

    printf("paopao GIF asset decode passed: %u GIFs sampled\n", (unsigned)(sizeof(k_gif_cases) / sizeof(k_gif_cases[0])));
    return 0;
}
