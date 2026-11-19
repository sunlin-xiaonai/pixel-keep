// Host tests for Pixel Keep's game core through the C bridge main.c uses (no device, no ESP-IDF).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../main/game/game.h"
#include "../main/pk_core.h"

static int failures;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static uint16_t be(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

int main() {
    static uint16_t frame[PK_SCREEN * PK_SCREEN];
    CHECK(pk_init(42, 0, false));
    CHECK(!strcmp(pk_state(), "title"));

    // The frame: black outside the circle and in the spare row and column, palette colours inside, 3x3 blocks.
    memset(frame, 0xAB, sizeof frame);
    pk_render_be(frame);
    CHECK(frame[0] == 0);
    CHECK(frame[PK_SCREEN - 1] == 0);
    CHECK(frame[(PK_SCREEN - 1) * PK_SCREEN + 233] == 0);
    bool in_palette = false;
    const uint16_t mid = frame[234 * PK_SCREEN + 234];
    for (int i = 0; i < 16; i++) in_palette |= mid == be(GAME_PALETTE[i]);
    CHECK(in_palette);
    int row_errors = 0;
    for (int y = 0; y < PK_SCREEN - 1; y += 3)
        for (int copy = 1; copy < 3; copy++)
            row_errors += memcmp(frame + y * PK_SCREEN, frame + (y + copy) * PK_SCREEN,
                                 PK_SCREEN * sizeof *frame) != 0;
    CHECK(row_errors == 0);
    int block_errors = 0;
    for (int y = 0; y < PK_SCREEN - 1; y += 3)
        for (int x = 0; x < PK_SCREEN - 1; x += 3)
            block_errors += frame[(y + 2) * PK_SCREEN + x + 2] != frame[y * PK_SCREEN + x];
    CHECK(block_errors == 0);

    // A tap on the title starts a run; the run goes on while aiming and with a bomb.
    pk_input_t in = {true, 233, 233, false};
    pk_step(1 / 30.0f, &in);
    CHECK(!strcmp(pk_state(), "play"));
    for (int i = 0; i < 300; i++) {
        in.down = i % 40 < 30;
        in.x = 233 + 150 * sinf(i * 0.05f);
        in.y = 233 + 150 * cosf(i * 0.05f);
        in.bomb = i == 150;
        pk_step(1 / 30.0f, &in);
    }
    CHECK(!strcmp(pk_state(), "play"));
    CHECK(pk_time() > 9.5f && pk_time() < 10.5f);
    CHECK(pk_sfx_count() > 0);

    // A long frame (a pause) is cut to 50 ms, not skipped ahead.
    const float t = pk_time();
    pk_step(2.0f, &in);
    CHECK(pk_time() - t < 0.06f);

    // The synth stays in -1..1.
    static float audio[2400];
    pk_audio(audio, 2400, 1.0f);
    int out_of_range = 0;
    for (float v : audio) out_of_range += v < -1.0f || v > 1.0f;
    CHECK(out_of_range == 0);

    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
