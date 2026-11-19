#pragma once
// Tiny chiptune synth for Pixel Keep: square / triangle / noise voices with exponential decay, 24 kHz mono.
// synth_trigger() may be called from the game thread while synth_render() runs on the audio thread.
#include <stddef.h>
#include <stdint.h>

#include "game.h"

constexpr int SYNTH_RATE = 24000;
void synth_trigger(GameSfx sfx, int param);  // lock-free single-producer queue
void synth_render(int16_t* out, size_t n, float gain);  // gain 1.0 = design loudness (web preview)
void synth_render_f(float* out, size_t n, float gain);   // the same mix as floats in -1..1
