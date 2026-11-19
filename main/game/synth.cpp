// Sound effects of the web design preview, reproduced sample by sample (oscillator + exponential gain ramp
// to 0.0001, noise with a linear fade). Volumes are the Web Audio gains of the preview.
#include "synth.h"

#include <math.h>
#include <string.h>

namespace {

enum Wave : uint8_t { SQUARE, TRIANGLE, NOISE };
struct Voice {
  bool on;
  Wave wave;
  int32_t delay, left, total;
  float phase, inc, inc_mul, gain, gain_mul, vol;
};
constexpr int VOICES = 16;
Voice g_v[VOICES];
uint32_t g_noise = 0x2468ACE;

struct Ev {
  uint8_t sfx;
  int16_t param;
};
constexpr int QN = 32;
Ev g_q[QN];
volatile uint32_t g_qh = 0, g_qt = 0;  // head: consumer, tail: producer

Voice* alloc_voice() {
  Voice* best = &g_v[0];
  for (auto& v : g_v) {
    if (!v.on) return &v;
    if (v.left < best->left) best = &v;
  }
  return best;
}

void tone(float f, float d, Wave w, float vol, float slide = 0, float delay = 0) {
  Voice* v = alloc_voice();
  int n = (int)(d * SYNTH_RATE);
  if (n < 1) n = 1;
  v->on = true;
  v->wave = w;
  v->delay = (int32_t)(delay * SYNTH_RATE);
  v->left = v->total = n;
  v->phase = 0;
  v->inc = f / SYNTH_RATE;
  v->inc_mul = slide > 0 ? powf(fmaxf(30.0f, f * slide) / f, 1.0f / n) : 1.0f;
  v->gain = 1.0f;
  v->gain_mul = powf(0.0001f, 1.0f / n);
  v->vol = vol;
}

void noise(float d, float vol, float delay = 0) {
  Voice* v = alloc_voice();
  int n = (int)(d * SYNTH_RATE);
  v->on = true;
  v->wave = NOISE;
  v->delay = (int32_t)(delay * SYNTH_RATE);
  v->left = v->total = n < 1 ? 1 : n;
  v->vol = vol;
  v->gain = 1.0f;
}

float semis(float base, float s) { return base * powf(2.0f, s / 12.0f); }

void play(GameSfx s, int param) {
  static const int PENTA[5] = {0, 2, 4, 7, 9};
  switch (s) {
    case GameSfx::Hit: tone(1200 + (g_noise % 300), 0.025f, SQUARE, 0.012f); break;
    case GameSfx::Pop: {
      int i = param < 1 ? 0 : (param - 1 > 19 ? 19 : param - 1);
      tone(semis(392, PENTA[i % 5] + 12 * (i / 5)), 0.1f, SQUARE, 0.045f);
      noise(0.06f, 0.05f);
      break;
    }
    case GameSfx::Pick: {
      const int st[4] = {0, 4, 7, 12};
      for (int i = 0; i < 4; i++) tone(semis(660, st[i]), 0.07f, SQUARE, 0.035f, 0, i * 0.05f);
      break;
    }
    case GameSfx::Castle:
      tone(150, 0.28f, SQUARE, 0.07f, 0.4f);
      noise(0.22f, 0.09f);
      break;
    case GameSfx::Boss:
      tone(196, 0.16f, SQUARE, 0.05f);
      tone(165, 0.32f, SQUARE, 0.05f, 1, 0.18f);
      break;
    case GameSfx::Praise: {
      const int st[5] = {0, 7, 12, 16, 19};
      for (int i = 0; i < 5; i++) tone(semis(784, st[i]), 0.08f, SQUARE, 0.03f, 0, i * 0.045f);
      break;
    }
    case GameSfx::Bomb:
      noise(0.45f, 0.14f);
      tone(110, 0.45f, TRIANGLE, 0.12f, 0.35f);
      break;
    case GameSfx::Over: {
      const int st[4] = {7, 4, 0, -5};
      for (int i = 0; i < 4; i++) tone(semis(392, st[i]), 0.2f, SQUARE, 0.045f, 0, i * 0.18f);
      break;
    }
    case GameSfx::Start:
      tone(523, 0.06f, SQUARE, 0.04f);
      tone(784, 0.1f, SQUARE, 0.04f, 0, 0.06f);
      break;
    case GameSfx::Empty: tone(220, 0.12f, SQUARE, 0.03f, 0.7f); break;
  }
}

}  // namespace

void synth_trigger(GameSfx sfx, int param) {
  uint32_t t = g_qt;
  if (t - g_qh >= QN) return;  // full: drop
  g_q[t % QN] = {(uint8_t)sfx, (int16_t)param};
  __sync_synchronize();
  g_qt = t + 1;
}

void synth_render_f(float* out, size_t n, float gain) {
  while (g_qh != g_qt) {
    Ev e = g_q[g_qh % QN];
    __sync_synchronize();
    g_qh = g_qh + 1;
    play((GameSfx)e.sfx, e.param);
  }
  for (size_t i = 0; i < n; i++) {
    float acc = 0;
    for (auto& v : g_v) {
      if (!v.on) continue;
      if (v.delay > 0) {
        v.delay--;
        continue;
      }
      float s;
      if (v.wave == NOISE) {
        g_noise ^= g_noise << 13;
        g_noise ^= g_noise >> 17;
        g_noise ^= g_noise << 5;
        s = ((int32_t)g_noise >> 8) * (1.0f / 8388608.0f) * ((float)v.left / v.total);
      } else {
        s = v.wave == SQUARE ? (v.phase < 0.5f ? 1.0f : -1.0f) : (4.0f * fabsf(v.phase - 0.5f) - 1.0f);
        v.phase += v.inc;
        if (v.phase >= 1.0f) v.phase -= 1.0f;
        v.inc *= v.inc_mul;
        s *= v.gain;
        v.gain *= v.gain_mul;
      }
      acc += s * v.vol;
      if (--v.left <= 0) v.on = false;
    }
    float o = acc * gain;
    out[i] = o > 0.98f ? 0.98f : o < -0.98f ? -0.98f : o;
  }
}

void synth_render(int16_t* out, size_t n, float gain) {
  float buf[256];
  while (n) {
    size_t k = n < 256 ? n : 256;
    synth_render_f(buf, k, gain);
    for (size_t i = 0; i < k; i++) out[i] = (int16_t)(buf[i] * 32767.0f);
    out += k;
    n -= k;
  }
}
