#include "pk_core.h"

#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "game/synth.h"

static_assert(PK_LOGICAL == GAME_W, "logical size");
static_assert(PK_SYNTH_RATE == SYNTH_RATE, "synth rate");
static_assert(PK_LOGICAL * GAME_SCALE + 1 == PK_SCREEN, "one spare row and column");

static uint8_t *s_frame;
static uint16_t s_be[16];
static volatile int s_sfx;

static void on_sfx(GameSfx sfx, int param) {
    s_sfx = s_sfx + 1;
    synth_trigger(sfx, param);
}

bool pk_init(uint32_t seed, float best_seconds, bool english) {
    if (!s_frame) s_frame = (uint8_t *)malloc(GAME_W * GAME_W);
    if (!s_frame) return false;
    for (int i = 0; i < 16; i++) s_be[i] = (uint16_t)((GAME_PALETTE[i] >> 8) | (GAME_PALETTE[i] << 8));
    game_init(seed, best_seconds, english, on_sfx);
    return true;
}

void pk_step(float dt, const pk_input_t *in) {
    GameInput g{};
    g.down = in->down;
    g.x = in->x / GAME_SCALE;
    g.y = in->y / GAME_SCALE;
    g.shake = in->bomb;
    game_step(dt, g);
}

void pk_render_be(uint16_t *out) {
    game_render(s_frame);
    uint16_t row[PK_SCREEN];
    for (int ly = 0; ly < GAME_W; ly++) {
        int x0, x1;
        game_row_span(ly, &x0, &x1);
        memset(row, 0, sizeof row);
        const uint8_t *src = s_frame + ly * GAME_W;
        for (int lx = x0; lx <= x1; lx++) {
            const uint16_t c = s_be[src[lx] & 15];
            uint16_t *d = row + lx * GAME_SCALE;
            d[0] = d[1] = d[2] = c;
        }
        uint16_t *dst = out + ly * GAME_SCALE * PK_SCREEN;
        for (int copy = 0; copy < GAME_SCALE; copy++) memcpy(dst + copy * PK_SCREEN, row, sizeof row);
    }
    memset(out + (PK_SCREEN - 1) * PK_SCREEN, 0, PK_SCREEN * 2);
}

const char *pk_state(void) { return game_state_name(); }
float pk_time(void) { return game_time(); }
float pk_best(void) { return game_best(); }
int pk_troops(void) { return game_troops(); }
int pk_objects(void) { return game_object_count(); }
bool pk_take_new_best(float *seconds) { return game_take_new_best(seconds); }
int pk_sfx_count(void) { return s_sfx; }
void pk_audio(float *out, size_t n, float gain) { synth_render_f(out, n, gain); }
