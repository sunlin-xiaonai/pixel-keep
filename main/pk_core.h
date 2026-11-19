// The C side of Pixel Keep's game core (main/game, C++): main.c drives it through these, and test/ checks them on
// the Mac. The logical frame is 155 x 155 palette indices, drawn 3x to the 466 x 466 panel.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PK_LOGICAL 155
#define PK_SCREEN 466

typedef struct {
    bool down;     // a finger on the screen
    float x, y;    // screen pixels (0..466), valid while down
    bool bomb;     // shaken or the side key tapped since the last step
} pk_input_t;

/** Allocates the frames (heap: PSRAM on the board) and starts on the title. false without memory. */
bool pk_init(uint32_t seed, float best_seconds, bool english);
void pk_step(float dt, const pk_input_t *in);
/** Draws the frame and scales it into `out`: PK_SCREEN x PK_SCREEN big-endian RGB565 (the panel's byte order),
 *  black outside the round field and in the last row and column. */
void pk_render_be(uint16_t *out);

/** "title", "play" or "over". */
const char *pk_state(void);
float pk_time(void);
float pk_best(void);
int pk_troops(void);
int pk_objects(void);
/** True once after a run beat the record; `seconds` the new record. */
bool pk_take_new_best(float *seconds);
/** Sound effects queued since the start (the game triggers them; the synth plays them). */
int pk_sfx_count(void);
/** The synth's next `n` samples at PK_SYNTH_RATE, floats in -1..1. */
void pk_audio(float *out, size_t n, float gain);
#define PK_SYNTH_RATE 24000

#ifdef __cplusplus
}
#endif
