// Pixel Keep (像素守城) game core. Tuned for a relaxed pace:
// slower and smaller waves, more supplies when troops run low, and stored bombs released by shaking the board.
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_text.h"
#include "yk_round.h"

namespace {

enum : uint8_t { K, N_, M, G, O, D, L, Wt, R, Y, Yl, Gl, B, V, P, Sk };
constexpr uint8_t T = 255;  // transparent
constexpr int W = GAME_W, C = 77;
constexpr float PI = 3.14159265f, TAU = 6.2831853f;
constexpr uint32_t SEAT_COLORS[] = YK_SEAT_COLORS;
bool g_english;

constexpr uint16_t rgb565(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

constexpr uint32_t shade(uint32_t rgb, unsigned percent) {
  return ((((rgb >> 16) & 255) * percent / 100) << 16) |
         ((((rgb >> 8) & 255) * percent / 100) << 8) |
         ((rgb & 255) * percent / 100);
}

int idx_of(char ch) {
  static const char* keys = "knmgodlwryYGbvps";
  const char* p = strchr(keys, ch);
  return p ? (int)(p - keys) : T;
}

// ---------------------------------------------------------------- random
uint32_t g_rng = 1;
float frand() {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return (g_rng >> 8) * (1.0f / 16777216.0f);
}
float rnd(float a, float b) { return a + frand() * (b - a); }
int iround(float v) { return (int)floorf(v + 0.5f); }

// ---------------------------------------------------------------- sprites
struct Sprite {
  uint8_t w = 0, h = 0;
  uint8_t px[25 * 31];
};

void sprite_from(Sprite& s, const char* const* rows, int h, int sub = -1) {
  s.h = h;
  s.w = strlen(rows[0]);
  for (int j = 0; j < h; j++)
    for (int i = 0; i < s.w; i++) {
      char ch = rows[j][i];
      s.px[j * s.w + i] = ch == '.' ? T : (sub >= 0 ? sub : idx_of(ch));
    }
}

const char* SOLDIER_R[] = {"bbb", "sss", "bnb", "n.n"};
const char* SOLDIER_FIRE_R[] = {"YYY", "sss", "bnb", "n.n"};
const char* SL_A[] = {"..rrr..", ".rrrrr.", "rwkrwkr", "rrrrrrr", "rprrrpr", ".mmmmm."};
const char* SL_B[] = {".......", ".rrrrr.", "rwkrwkr", "rrrrrrr", "rprrrpr", "mmmmmmm"};
const char* BOSS_R[] = {".....Y.Y.Y.....", ".....YYYYY.....", "....rrrrrrr....", "..rrwrrrrrrrr..",
                        ".rrrrrrrrrrrrr.", ".rrwwkrrrwwkrr.", "rrrwkkrrrwkkrrr", "rrrrrrrrrrrrrrr",
                        "rrprrrkkkrrrprr", "rrrrrrkkkrrrrrr", "rrrrrrrrrrrrrrr", ".mmmmmmmmmmmmm.",
                        "..mmmmmmmmmmm.."};
const char* BOMB_HUD_R[] = {"....Y", "...y.", ".kkn.", "kkkkk", "kwkkk", "kkkkk", ".kkk."};
Sprite BOMB_HUD, SOLDIER, SOLDIER_FIRE, SLIME[2], SLIME_F[2], BOSS, BOSS_F, CASTLE[2], CASTLE_H[2];

enum PkKind { PK_ADD, PK_DBL, PK_RATE, PK_SHIELD, PK_BOMB, PK_COUNT };
Sprite PK_IMG[PK_COUNT];
const uint8_t PK_COL[PK_COUNT] = {Gl, Yl, B, V, R};

void make_tile(Sprite& s, uint8_t c, const char* const* icon, uint8_t ic) {
  s.w = s.h = 9;
  for (int j = 0; j < 9; j++)
    for (int i = 0; i < 9; i++) {
      bool corner = (i == 0 || i == 8) && (j == 0 || j == 8);
      bool edge = i == 0 || i == 8 || j == 0 || j == 8;
      s.px[j * 9 + i] = corner ? T : edge ? static_cast<uint8_t>(K) : c;
    }
  s.px[1 * 9 + 1] = Wt;
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < 5; i++)
      if (icon[j][i] == '1') s.px[(j + 2) * 9 + i + 2] = ic;
}

void make_castle(Sprite& s, int frame, bool hurt) {
  s.w = 25;
  s.h = 31;
  memset(s.px, T, sizeof(s.px));
  const int cx = 12, cy = 18;
  auto p = [&](int i, int j, uint8_t k) {
    if (i >= 0 && i < 25 && j >= 0 && j < 31) s.px[j * 25 + i] = k;
  };
  for (int j = 0; j < 31; j++)
    for (int i = 0; i < 25; i++) {
      float d = hypotf(i - cx, j - cy);
      if (d > 11.5f) continue;
      uint8_t k = d > 10.6f ? (hurt ? R : K) : d > 8.2f ? L : d > 7.4f ? D : (((i * 3 + j) % 5 == 0) ? D : V);
      p(i, j, k);
    }
  for (int q = 0; q < 16; q++) {
    float a = q / 16.0f * TAU;
    p(iround(cx + cosf(a) * 9.4f), iround(cy + sinf(a) * 9.4f), q % 2 ? D : Wt);
  }
  for (int j = -3; j <= 3; j++)
    for (int i = -3; i <= 3; i++) {
      bool e = abs(i) == 3 || abs(j) == 3;
      p(cx + i, cy + j, e ? K : (i == j || i == -j) ? M : R);
    }
  p(cx - 2, cy - 2, P);
  for (int j = 5; j <= 15; j++) p(cx, j, L);
  const int fl[2][3][2] = {{{1, 4}, {1, 4}, {1, 3}}, {{1, 3}, {1, 4}, {1, 4}}};
  for (int r = 0; r < 3; r++)
    for (int i = fl[frame][r][0]; i <= fl[frame][r][1]; i++)
      p(cx + i, 5 + r, (i == fl[frame][r][1] && r == 1) ? N_ : B);
}

// ---------------------------------------------------------------- frame buffer helpers
uint8_t* g_fb;
int g_ox, g_oy;  // screen shake offset (world layer only)
uint8_t* g_bg;  // W * W, on the heap: PSRAM on the board, not internal RAM
uint8_t g_span0[W], g_span1[W];

inline void px(float x, float y, uint8_t c) {
  int ix = iround(x) + g_ox, iy = iround(y) + g_oy;
  if ((unsigned)ix < (unsigned)W && (unsigned)iy < (unsigned)W) g_fb[iy * W + ix] = c;
}

void put(const Sprite& s, float cx, float cy) {
  int x0 = iround(cx - s.w / 2.0f) + g_ox, y0 = iround(cy - s.h / 2.0f) + g_oy;
  for (int j = 0; j < s.h; j++) {
    int y = y0 + j;
    if ((unsigned)y >= (unsigned)W) continue;
    const uint8_t* src = s.px + j * s.w;
    uint8_t* dst = g_fb + y * W;
    for (int i = 0; i < s.w; i++) {
      int x = x0 + i;
      if (src[i] != T && (unsigned)x < (unsigned)W) dst[x] = src[i];
    }
  }
}

void arc_px(float r, float a0, float a1, uint8_t c) {
  for (float a = a0; a <= a1; a += 0.6f / r) px(C + cosf(a) * r, C + sinf(a) * r, c);
}

// 3x5 digits and signs, drawn with a 1 px black outline
struct Glyph {
  char ch;
  uint8_t w;
  const char* rows[5];
};
const Glyph F3[] = {
    {'0', 3, {"111", "101", "101", "101", "111"}}, {'1', 3, {"010", "110", "010", "010", "111"}},
    {'2', 3, {"111", "001", "111", "100", "111"}}, {'3', 3, {"111", "001", "011", "001", "111"}},
    {'4', 3, {"101", "101", "111", "001", "001"}}, {'5', 3, {"111", "100", "111", "001", "111"}},
    {'6', 3, {"111", "100", "111", "101", "111"}}, {'7', 3, {"111", "001", "010", "010", "010"}},
    {'8', 3, {"111", "101", "111", "101", "111"}}, {'9', 3, {"111", "101", "111", "001", "111"}},
    {':', 1, {"0", "1", "0", "1", "0"}},           {'+', 3, {"000", "010", "111", "010", "000"}},
    {'-', 3, {"000", "000", "111", "000", "000"}}, {'x', 3, {"000", "101", "010", "101", "000"}},
};
const Glyph& glyph(char ch) {
  for (const auto& g : F3)
    if (g.ch == ch) return g;
  return F3[0];
}

void num_layer(const char* s, int x0, int y0, int sc, uint8_t c) {
  int cx = 0;
  for (const char* q = s; *q; q++) {
    const Glyph& g = glyph(*q);
    for (int j = 0; j < 5; j++)
      for (int i = 0; i < g.w; i++)
        if (g.rows[j][i] == '1')
          for (int b = 0; b < sc; b++)
            for (int a = 0; a < sc; a++) {
              int x = x0 + (cx + i) * sc + a, y = y0 + j * sc + b;
              if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)W) g_fb[y * W + x] = c;
            }
    cx += g.w + 1;
  }
}

void num(const char* s, float cx, float cy, uint8_t c = Wt, int sc = 1) {
  int w0 = -1;
  for (const char* q = s; *q; q++) w0 += glyph(*q).w + 1;
  int w = w0 * sc + 2, h = 5 * sc + 2;
  int x0 = iround(cx - w / 2.0f) + g_ox, y0 = iround(cy - h / 2.0f) + g_oy;
  static const int8_t OUT[8][2] = {{0, 1}, {2, 1}, {1, 0}, {1, 2}, {0, 0}, {2, 2}, {0, 2}, {2, 0}};
  for (auto& o : OUT) num_layer(s, x0 + o[0], y0 + o[1], sc, K);
  num_layer(s, x0 + 1, y0 + 1, sc, c);
}

void numi(int v, float cx, float cy, uint8_t c = Wt, int sc = 1) {
  char b[12];
  snprintf(b, sizeof b, "%d", v);
  num(b, cx, cy, c, sc);
}

void fmt_time(float s, char* out) {
  unsigned t = s > 0 ? (unsigned)s : 0;
  if (t > 99 * 60 + 59) t = 99 * 60 + 59;
  snprintf(out, 8, "%u:%02u", t / 60, t % 60);
}

const LabelBitmap* label_find(const char* key) {
  for (const auto& bitmap : g_english ? LABELS_EN : LABELS_ZH)
    if (strcmp(bitmap.key, key) == 0) return &bitmap;
  return nullptr;
}

void label_layer(const LabelBitmap* t, int x0, int y0, uint8_t c) {
  int stride = (t->w + 7) / 8;
  for (int j = 0; j < t->h; j++)
    for (int i = 0; i < t->w; i++)
      if (t->bits[j * stride + i / 8] & (0x80 >> (i % 8))) {
        int x = x0 + i, y = y0 + j;
        if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)W) g_fb[y * W + x] = c;
      }
}

void label(const char* key, float cx, float cy, uint8_t c = Wt, int shadow = -1) {
  const LabelBitmap* t = label_find(key);
  if (!t) return;
  int w = t->w + 4, h = t->h + 3;
  int x0 = iround(cx - w / 2.0f) + g_ox, y0 = iround(cy - h / 2.0f) + g_oy;
  if (shadow >= 0) label_layer(t, x0 + 1, y0 + 3, shadow);
  static const int8_t OUT[4][2] = {{0, 1}, {2, 1}, {1, 0}, {1, 2}};
  for (auto& o : OUT) label_layer(t, x0 + o[0], y0 + o[1], K);
  label_layer(t, x0 + 1, y0 + 1, c);
}

// ---------------------------------------------------------------- game state
// Relaxed tuning (design: "更休闲"): bigger start army, slower spawns, smaller waves, mercy supplies when low.
constexpr int START_TROOPS = 40;
constexpr float BOSS_EVERY = 40.0f;
constexpr float BOMB_RECHARGE = 25.0f;
constexpr int BOMB_MAX = 2;
constexpr float RING_R = 62.0f;  // supply crates orbit here

struct Group {
  float a, r, flash, seed, sp;
  int n, max;
  bool boss, dead;
};
struct Arrow {
  float x, y, vx, vy;
  bool dead;
};
struct Pick {
  int k;
  float a, life;
  bool dead;
};
struct Part {
  float x, y, vx, vy, life;
  uint8_t c;
};
struct Decal {
  int16_t x, y;
  float life;
  uint8_t c;
};
struct Float {
  float x, y, life;
  char txt[16];
  bool is_cn;
  uint8_t c;
};
struct Slot {
  float x, y, a, fire;
};

template <typename Tp, int Cap>
struct Pool {
  Tp v[Cap];
  int n = 0;
  Tp* add() { return n < Cap ? &v[n++] : nullptr; }
  template <typename F>
  void keep_if(F f) {
    int j = 0;
    for (int i = 0; i < n; i++)
      if (f(v[i])) v[j++] = v[i];
    n = j;
  }
};

enum class St { Title, Play, Over };

struct State {
  St st;
  float t, aim, fire, rate, spawn, boss, pkcd, comboT, shake, hs, hurt, frame, overT, last, bombT, emptyT, hintT;
  int N, shield, combo, bombs;
  bool newBest;
  struct {
    bool on;
    float r;
    uint32_t hit;  // bit per group index at the time of launch is not stable; use per-group flag instead
  } wave;
  struct {
    const char* txt;
    float t;
  } praise, banner;
  Pool<Group, 40> groups;
  Pool<Arrow, 120> arrows;
  Pool<Pick, 8> pks;
  Pool<Part, 500> parts;
  Pool<Decal, 700> decals;
  Pool<Float, 16> floats;
};

State S;
Slot g_slots[45];
const int8_t OFFS[7][2] = {{0, 0}, {-7, 1}, {7, 1}, {-4, -5}, {4, -5}, {-4, 6}, {4, 6}};
uint8_t g_wave_hit[40];
float g_best = 0;
bool g_new_best_pending = false;
GameSfxCb g_sfx = nullptr;

void sfx(GameSfx s, int p = 0) {
  if (g_sfx) g_sfx(s, p);
}

void reset_state() {
  S.st = St::Play;
  S.t = 0;
  S.N = START_TROOPS;
  S.aim = -PI / 2;
  S.fire = 0;
  S.rate = 0.13f;
  S.shield = 0;
  S.spawn = 1.2f;
  S.boss = BOSS_EVERY;
  S.pkcd = 3;
  S.combo = 0;
  S.comboT = 0;
  S.shake = S.hs = S.hurt = 0;
  S.overT = S.last = 0;
  S.bombs = 1;
  S.bombT = BOMB_RECHARGE;
  S.emptyT = 0;
  S.hintT = 6;
  S.newBest = false;
  S.wave.on = false;
  S.praise = {nullptr, 0};
  S.banner = {nullptr, 0};
  S.groups.n = S.arrows.n = S.pks.n = S.parts.n = S.decals.n = S.floats.n = 0;
}

void title_scene() {
  reset_state();
  S.st = St::Title;
  S.N = 24;
  for (int i = 0; i < 6; i++) {
    Group* g = S.groups.add();
    *g = {i / 6.0f * TAU + 0.4f, 46.0f + (i % 2) * 10, 0, frand() * 9, 0, 3 + i, 3 + i, false, false};
  }
}

void gpos(const Group& g, float* x, float* y) {
  *x = C + cosf(g.a) * g.r;
  *y = C + sinf(g.a) * g.r;
}
float grr(const Group& g) { return g.boss ? 7.0f : 4.0f + fminf(5.0f, sqrtf((float)g.n) * 1.2f); }

void spawn_group(bool boss) {
  Group* g = S.groups.add();
  if (!g) return;
  float t = S.t;
  int n = boss ? iround(35 + t * 0.5f) : iround(3 + t * 0.07f + frand() * 3);
  float sp = boss ? 3.5f : fminf(12.0f, 6.0f + t * 0.025f);
  *g = {rnd(0, TAU), 80, 0, frand() * 9, sp, n, n, boss, false};
  if (boss) {
    S.banner = {"果冻大王 来了!", 2.2f};
    sfx(GameSfx::Boss);
  }
}

void burst(float x, float y, const uint8_t* cols, int nc, int n, float sp = 40) {
  for (int i = 0; i < n; i++) {
    Part* p = S.parts.add();
    if (!p) return;
    float a = rnd(0, TAU), v = rnd(sp * 0.3f, sp);
    *p = {x, y, cosf(a) * v, sinf(a) * v, rnd(0.3f, 0.7f), cols[i % nc]};
  }
}

void splat(float x, float y, int n, float r) {
  for (int i = 0; i < n; i++) {
    Decal* d = S.decals.add();
    if (!d) return;
    float a = rnd(0, TAU), dd = rnd(0, r);
    *d = {(int16_t)iround(x + cosf(a) * dd), (int16_t)iround(y + sinf(a) * dd), rnd(2.5f, 4.5f), frand() < 0.6f ? M : R};
  }
}

void float_txt(float x, float y, const char* txt, uint8_t c, bool is_cn) {
  Float* f = S.floats.add();
  if (!f) return;
  f->x = x;
  f->y = y;
  f->life = 0.9f;
  f->c = c;
  f->is_cn = is_cn;
  snprintf(f->txt, sizeof f->txt, "%s", txt);
}

const uint8_t COL_GOO[] = {R, P, Wt, M};

void kill(Group& g, float x, float y) {
  g.dead = true;
  burst(x, y, COL_GOO, 4, g.boss ? 60 : 14, g.boss ? 70 : 40);
  splat(x, y, g.boss ? 40 : 8, g.boss ? 10 : 4);
  S.combo++;
  S.comboT = 1.6f;
  sfx(GameSfx::Pop, S.combo);
  S.shake = fmaxf(S.shake, g.boss ? 5.0f : (g.max >= 15 ? 2.0f : 1.0f));
  if (g.max >= 15 || g.boss) S.hs = g.boss ? 0.16f : 0.05f;
  if (g.boss) {
    Pick* p = S.pks.add();
    if (p) *p = {PK_DBL, g.a, 10, false};
  }
  const char* praise = S.combo == 5 ? "好!" : S.combo == 10 ? "妙!" : S.combo == 20 ? "爽!" : S.combo == 40 ? "神!" : nullptr;
  if (praise) {
    S.praise = {praise, 0.9f};
    sfx(GameSfx::Praise);
  }
}

void launch_wave() {
  S.wave.on = true;
  S.wave.r = 12;
  memset(g_wave_hit, 0, sizeof g_wave_hit);
  sfx(GameSfx::Bomb);
  S.shake = fmaxf(S.shake, 3.0f);
}

void apply_pick(const Pick& p, float x, float y) {
  static const char* LABEL[PK_COUNT] = {"+10", "x2", "连射", "护盾", "轰!"};
  switch (p.k) {
    case PK_ADD: S.N += 10; break;
    case PK_DBL: S.N = S.N * 2 < S.N + 200 ? S.N * 2 : S.N + 200; break;
    case PK_RATE: S.rate = fmaxf(0.05f, S.rate * 0.8f); break;
    case PK_SHIELD: S.shield++; break;
    case PK_BOMB: launch_wave(); break;
  }
  float_txt(x, y, LABEL[p.k], PK_COL[p.k], p.k >= PK_RATE);
  uint8_t cols[2] = {PK_COL[p.k], Wt};
  burst(x, y, cols, 2, 12, 30);
  if (p.k != PK_BOMB) sfx(GameSfx::Pick);
}

void use_bomb() {
  if (S.st != St::Play) return;
  if (S.bombs > 0 && !S.wave.on) {
    S.bombs--;
    launch_wave();
    float_txt(C, C - 30, "轰!", R, true);
  } else if (S.emptyT <= 0) {
    S.emptyT = 1.0f;
    float_txt(C, C - 30, "充能中", L, true);
    sfx(GameSfx::Empty);
  }
}

float ang_diff(float a, float b) {
  float d = fmodf(a - b, TAU);
  if (d < 0) d += TAU;
  if (d > PI) d -= TAU;
  return fabsf(d);
}

void game_over() {
  S.N = 0;
  S.st = St::Over;
  S.last = S.t;
  S.newBest = S.t > g_best;
  if (S.newBest) {
    g_best = S.t;
    g_new_best_pending = true;
  }
  static const uint8_t cols[] = {L, D, R, Wt, B};
  burst(C, C, cols, 5, 70, 60);
  sfx(GameSfx::Over);
}

void update(float dt) {
  S.frame += dt;
  for (int i = 0; i < S.parts.n; i++) {
    Part& p = S.parts.v[i];
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    float f = powf(0.03f, dt);
    p.vx *= f;
    p.vy *= f;
    p.life -= dt;
  }
  for (int i = 0; i < S.decals.n; i++) S.decals.v[i].life -= dt;
  for (int i = 0; i < S.floats.n; i++) {
    S.floats.v[i].life -= dt;
    S.floats.v[i].y -= 14 * dt;
  }
  S.praise.t -= dt;
  S.banner.t -= dt;
  S.hintT -= dt;
  S.emptyT -= dt;
  S.shake = fmaxf(0, S.shake - dt * 14);
  S.hurt = fmaxf(0, S.hurt - dt);
  S.parts.keep_if([](const Part& p) { return p.life > 0; });
  S.decals.keep_if([](const Decal& d) { return d.life > 0; });
  S.floats.keep_if([](const Float& f) { return f.life > 0; });
  if (S.st == St::Over) {
    S.overT += dt;
    return;
  }
  if (S.st != St::Play) return;
  if (S.hs > 0) {
    S.hs -= dt;
    return;
  }
  S.t += dt;
  bool low = S.N < 15;

  S.spawn -= dt;
  if (S.spawn <= 0) {
    spawn_group(false);
    S.spawn = fmaxf(1.5f, 3.0f - S.t * 0.012f) * (low ? 1.5f : 1.0f);
  }
  S.boss -= dt;
  if (S.boss <= 0) {
    spawn_group(true);
    S.boss = BOSS_EVERY;
  }
  S.pkcd -= dt;
  if (S.pkcd <= 0) {
    Pick* p = S.pks.add();
    if (p) *p = {low ? (frand() < 0.5f ? PK_ADD : PK_SHIELD) : (int)(frand() * static_cast<int>(PK_COUNT)),
                 rnd(0, TAU), 10, false};
    S.pkcd = low ? 3.0f : rnd(4, 6);
  }
  if (S.bombs < BOMB_MAX) {
    S.bombT -= dt;
    if (S.bombT <= 0) {
      S.bombs++;
      S.bombT = BOMB_RECHARGE;
    }
  }
  if (S.comboT > 0) {
    S.comboT -= dt;
    if (S.comboT <= 0) S.combo = 0;
  }

  S.fire -= dt;
  int vis = S.N < 45 ? S.N : 45;
  while (S.fire <= 0) {
    S.fire += S.rate;
    int k = S.N >= 100 ? 3 : S.N >= 50 ? 2 : 1;
    for (int i = 0; i < k; i++) {
      float a = S.aim + (i - (k - 1) / 2.0f) * 0.08f;
      float ox = C + cosf(a) * 12, oy = C + sinf(a) * 12;
      int cand[45], nc = 0;
      for (int j = 0; j < vis; j++)
        if (ang_diff(g_slots[j].a, a) < 0.5f) cand[nc++] = j;
      if (nc) {
        Slot& s = g_slots[cand[(int)(frand() * nc) % nc]];
        ox = s.x;
        oy = s.y;
        s.fire = 0.08f;
      }
      Arrow* b = S.arrows.add();
      if (b) *b = {ox, oy, cosf(a) * 130, sinf(a) * 130, false};
    }
  }
  for (auto& s : g_slots)
    if (s.fire > 0) s.fire = fmaxf(0, s.fire - dt);

  int dmg = 1 + S.N / 40;
  static const uint8_t COL_HIT[] = {R, P};
  for (int bi = 0; bi < S.arrows.n; bi++) {
    Arrow& b = S.arrows.v[bi];
    b.x += b.vx * dt;
    b.y += b.vy * dt;
    if (hypotf(b.x - C, b.y - C) > 78) {
      b.dead = true;
      continue;
    }
    for (int gi = 0; gi < S.groups.n; gi++) {
      Group& g = S.groups.v[gi];
      if (g.dead) continue;
      float x, y;
      gpos(g, &x, &y);
      if (hypotf(b.x - x, b.y - y) < grr(g) + 1.5f) {
        b.dead = true;
        g.n -= dmg;
        g.flash = 0.06f;
        sfx(GameSfx::Hit);
        burst(b.x, b.y, COL_HIT, 2, 2, 25);
        if (g.n <= 0) kill(g, x, y);
        break;
      }
    }
    if (b.dead) continue;
    for (int pi = 0; pi < S.pks.n; pi++) {
      Pick& p = S.pks.v[pi];
      if (p.dead) continue;
      float x = C + cosf(p.a) * RING_R, y = C + sinf(p.a) * RING_R;
      if (hypotf(b.x - x, b.y - y) < 5.5f) {
        p.dead = true;
        b.dead = true;
        apply_pick(p, x, y);
        break;
      }
    }
  }
  if (S.wave.on) {
    S.wave.r += 150 * dt;
    for (int gi = 0; gi < S.groups.n; gi++) {
      Group& g = S.groups.v[gi];
      if (g.dead || g_wave_hit[gi] || fabsf(g.r - S.wave.r) >= 5) continue;
      g_wave_hit[gi] = 1;
      g.n -= g.boss ? 60 : 30;
      g.flash = 0.1f;
      float x, y;
      gpos(g, &x, &y);
      if (g.n <= 0) kill(g, x, y);
    }
    if (S.wave.r > 80) S.wave.on = false;
  }
  static const uint8_t COL_SHIELD[] = {V, Wt};
  static const uint8_t COL_WALL[] = {L, D, K};
  for (int gi = 0; gi < S.groups.n; gi++) {
    Group& g = S.groups.v[gi];
    if (g.dead) continue;
    g.flash = fmaxf(0, g.flash - dt);
    g.r -= g.sp * dt;
    g.a += sinf(S.t * 2 + g.seed) * 0.0015f;
    if (g.r <= 15 + grr(g) * 0.3f) {
      g.dead = true;
      float x, y;
      gpos(g, &x, &y);
      S.shake = 4;
      S.hurt = 0.3f;
      if (S.shield > 0) {
        S.shield--;
        float_txt(x, y, "挡住", V, true);
        burst(x, y, COL_SHIELD, 2, 16, 40);
        sfx(GameSfx::Pick);
      } else {
        S.N -= g.n;
        char b[8];
        snprintf(b, sizeof b, "-%d", g.n);
        float_txt(x, y, b, R, false);
        burst(C, C, COL_WALL, 3, 18, 45);
        sfx(GameSfx::Castle);
      }
    }
  }
  for (int pi = 0; pi < S.pks.n; pi++) {
    Pick& p = S.pks.v[pi];
    p.a += 0.25f * dt;
    p.life -= dt;
    if (p.life <= 0) p.dead = true;
  }
  S.arrows.keep_if([](const Arrow& b) { return !b.dead; });
  // groups: the wave-hit flags follow their group when the pool compacts
  {
    int j = 0;
    for (int i = 0; i < S.groups.n; i++)
      if (!S.groups.v[i].dead) {
        g_wave_hit[j] = g_wave_hit[i];
        S.groups.v[j++] = S.groups.v[i];
      }
    S.groups.n = j;
  }
  S.pks.keep_if([](const Pick& p) { return !p.dead; });
  if (S.N <= 0) game_over();
}

// ---------------------------------------------------------------- render
uint8_t frame_bit(float speed, float seed = 0) { return (uint8_t)((int)floorf(S.frame * speed + seed) & 1); }

void draw_groups() {
  for (int gi = 0; gi < S.groups.n; gi++) {
    const Group& g = S.groups.v[gi];
    float x, y;
    gpos(g, &x, &y);
    int bob = frame_bit(5, g.seed);
    if (g.boss) {
      bool fl = g.flash > 0 && ((int)floorf(S.frame * 20) % 3 == 0);
      put(fl ? BOSS_F : BOSS, x, y + bob);
      numi(g.n, x, y - 12);
      continue;
    }
    int m = (g.n + 1) / 2;
    m = m < 1 ? 1 : m > 7 ? 7 : m;
    for (int i = m - 1; i >= 0; i--) {
      int f = frame_bit(5, g.seed + i);
      put(g.flash > 0 ? SLIME_F[f] : SLIME[f], x + OFFS[i][0], y + OFFS[i][1]);
    }
    numi(g.n, x, y - grr(g) - 5);
  }
}

void draw_soldiers(bool front) {
  int vis = S.st == St::Over ? 0 : (S.N < 45 ? S.N : 45);
  for (int j = 0; j < vis; j++) {
    const Slot& s = g_slots[j];
    if ((s.y >= C) == front) put(s.fire > 0 ? SOLDIER_FIRE : SOLDIER, s.x, s.y);
  }
}

}  // namespace

const uint16_t GAME_PALETTE[16] = {
    rgb565(YK_NIGHT), rgb565(YK_PAPER), rgb565(shade(YK_CORAL, 45)), rgb565(YK_PANEL),
    rgb565(shade(YK_AMBER, 40)), rgb565(SEAT_COLORS[2]), rgb565(YK_INK2), rgb565(YK_INK),
    rgb565(YK_CORAL), rgb565(YK_AMBER), rgb565(YK_CREAM), rgb565(SEAT_COLORS[6]),
    rgb565(YK_SAGE_TEXT), rgb565(YK_SAGE), rgb565(YK_CORAL_TEXT), rgb565(SEAT_COLORS[7]),
};

void game_row_span(int y, int* x0, int* x1) {
  *x0 = g_span0[y];
  *x1 = g_span1[y];
}

void game_render(uint8_t* frame) {
  g_fb = frame;
  g_ox = g_oy = 0;
  memcpy(frame, g_bg, W * W);
  if (S.shake > 0) {
    g_ox = iround(rnd(-S.shake, S.shake));
    g_oy = iround(rnd(-S.shake, S.shake));
    if (g_ox || g_oy) {
      // shift the ground with the world so the whole screen shakes
      memset(frame, K, W * W);
      for (int y = 0; y < W; y++) {
        int sy = y - g_oy;
        if ((unsigned)sy >= (unsigned)W) continue;
        for (int x = 0; x < W; x++) {
          int sx = x - g_ox;
          if ((unsigned)sx < (unsigned)W) frame[y * W + x] = g_bg[sy * W + sx];
        }
      }
    }
  }
  int blink8 = (int)floorf(S.frame * 8);
  for (int i = 0; i < S.decals.n; i++) {
    const Decal& d = S.decals.v[i];
    if (d.life > 1 || ((d.x + d.y + blink8) & 1)) px(d.x, d.y, d.c);
  }
  for (int i = 0; i < S.pks.n; i++) {
    const Pick& p = S.pks.v[i];
    if (p.life < 2 && ((int)floorf(p.life * 8) & 1)) continue;
    put(PK_IMG[p.k], C + cosf(p.a) * RING_R, C + sinf(p.a) * RING_R + frame_bit(3));
  }
  draw_groups();
  if (S.st == St::Play) {
    for (int r = 24; r < 70; r += 4) px(C + cosf(S.aim) * r, C + sinf(S.aim) * r, L);
    float ax = C + cosf(S.aim) * 70, ay = C + sinf(S.aim) * 70;
    px(ax - 2, ay, Wt);
    px(ax + 2, ay, Wt);
    px(ax, ay - 2, Wt);
    px(ax, ay + 2, Wt);
  }
  for (int i = 0; i < S.arrows.n; i++) {
    const Arrow& b = S.arrows.v[i];
    float u = b.vx / 130, v = b.vy / 130;
    px(b.x, b.y, Wt);
    px(b.x - u * 1.5f, b.y - v * 1.5f, Yl);
    px(b.x - u * 3, b.y - v * 3, Y);
  }
  draw_soldiers(false);
  if (S.st != St::Over) put((S.hurt > 0 ? CASTLE_H : CASTLE)[frame_bit(3)], C, C - 6);
  draw_soldiers(true);
  if (S.shield > 0 && S.st == St::Play) arc_px(24, 0, TAU, frame_bit(4) ? V : B);
  if (S.wave.on) {
    arc_px(S.wave.r, 0, TAU, Wt);
    arc_px(S.wave.r - 1.5f, 0, TAU, Yl);
  }
  for (int i = 0; i < S.parts.n; i++) px(S.parts.v[i].x, S.parts.v[i].y, S.parts.v[i].c);
  for (int i = 0; i < S.floats.n; i++) {
    const Float& f = S.floats.v[i];
    if (f.is_cn)
      label(f.txt, f.x, f.y, f.c);
    else
      num(f.txt, f.x, f.y, f.c);
  }
  g_ox = g_oy = 0;
  char tb[8];
  if (S.st == St::Play) {
    numi(S.N, C, C + 10, Wt, 2);
    fmt_time(S.t, tb);
    num(tb, C, 18, Wt, 2);
    const Group* boss = nullptr;
    for (int i = 0; i < S.groups.n; i++)
      if (S.groups.v[i].boss) boss = &S.groups.v[i];
    float a0 = -PI / 2 - 0.55f, a1 = -PI / 2 + 0.55f;
    arc_px(73, a0, a1, N_);
    if (boss)
      arc_px(73, a0, a0 + (a1 - a0) * fmaxf(0, (float)boss->n / boss->max), R);
    else
      arc_px(73, a0, a0 + (a1 - a0) * (1 - S.boss / BOSS_EVERY), V);
    for (int i = 0; i < S.bombs; i++) {
      float bx = C - (S.bombs - 1) * 6 + i * 12;
      put(BOMB_HUD, bx, 126);
    }
    if (S.combo >= 2) {
      label("连击", C - 16, 137, Yl);
      char cb[8];
      snprintf(cb, sizeof cb, "x%d", S.combo);
      num(cb, C + 19, 137, Yl, 2);
    }
    if (S.praise.t > 0 && S.praise.txt) label(S.praise.txt, C, C - 34, Yl, O);
    if (S.banner.t > 0 && S.banner.txt) label(S.banner.txt, C, C + 34, R);
    else if (S.hintT > 0) label("摇一摇 放炸弹", C, C + 34);
  } else if (S.st == St::Title) {
    label("像素守城", C, 40, Yl, O);
    label("点一下 开始", C, 114);
    if (g_best > 0) {
      label("最高", C - 14, 130, Yl);
      fmt_time(g_best, tb);
      num(tb, C + 14, 130, Yl);
    }
  } else if (S.overT > 0.6f) {
    for (int y = 0; y < W; y++)
      for (int x = (y & 1) ^ 1; x < W; x += 2) frame[y * W + x] = K;
    label("城破了", C, 42, R, M);
    label("坚守了", C, 62, L);
    fmt_time(S.last, tb);
    num(tb, C, 80, Wt, 3);
    if (S.newBest) {
      label("新纪录!", C, 100, Gl);
    } else {
      label("最高", C - 14, 100, Yl);
      fmt_time(g_best, tb);
      num(tb, C + 14, 100, Yl);
    }
    label("点一下 再来", C, 122);
  }
  for (int y = 0; y < W; y++) {
    uint8_t* row = frame + y * W;
    if (g_span1[y] < g_span0[y]) {
      memset(row, K, W);
      continue;
    }
    memset(row, K, g_span0[y]);
    memset(row + g_span1[y] + 1, K, W - 1 - g_span1[y]);
  }
}

namespace {

void build_background() {
  uint64_t seed = 7;
  auto srnd = [&]() {
    seed = (seed * 16807) % 2147483647;
    return (float)seed / 2147483647.0f;
  };
  auto put1 = [](int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)W) g_bg[y * W + x] = c;
  };
  const uint8_t flowers[3] = {P, Yl, Wt};
  for (int y = 0; y < W; y++)
    for (int x = 0; x < W; x++) {
      float d = hypotf(x - C, y - C);
      uint8_t c;
      if (d > 78) {
        c = K;
      } else if (d <= 19) {
        c = srnd() < 0.13f ? D : O;
        if (d > 18) c = D;
      } else {
        c = G;
        float r = srnd();
        if (r < 0.018f)
          c = N_;
        else if (r < 0.024f)
          c = flowers[(int)(srnd() * 3)];
        if (fabsf(d - 62) < 0.6f && ((int)floorf((atan2f(y - C, x - C) + 4) * 20)) % 2 == 0) c = N_;
      }
      if (d > 69 && d <= 78) {
        float k = (d - 69) / 9;
        if (k > 0.25f && ((x + y) & 1) == 0) c = N_;
        if (k > 0.6f) c = ((x + y) & 1) ? N_ : K;
      }
      put1(x, y, c);
    }
  for (int i = 0; i < 90; i++) {
    float a = srnd() * 7, r = 22 + srnd() * 44;
    int x = iround(C + cosf(a) * r), y = iround(C + sinf(a) * r);
    put1(x, y, Gl);
    put1(x - 1, y - 1, Gl);
    put1(x + 1, y - 1, Gl);
  }
  for (int y = 0; y < W; y++) {
    g_span0[y] = 1;
    g_span1[y] = 0;
    for (int x = 0; x < W; x++)
      if (hypotf(x - C, y - C) <= 77.6f) {
        if (g_span1[y] < g_span0[y]) g_span0[y] = x;
        g_span1[y] = x;
      }
  }
}

void build_sprites() {
  sprite_from(SOLDIER, SOLDIER_R, 4);
  sprite_from(SOLDIER_FIRE, SOLDIER_FIRE_R, 4);
  sprite_from(SLIME[0], SL_A, 6);
  sprite_from(SLIME[1], SL_B, 6);
  sprite_from(SLIME_F[0], SL_A, 6, Wt);
  sprite_from(SLIME_F[1], SL_B, 6, Wt);
  sprite_from(BOMB_HUD, BOMB_HUD_R, 7);
  sprite_from(BOSS, BOSS_R, 13);
  sprite_from(BOSS_F, BOSS_R, 13, Wt);
  for (int f = 0; f < 2; f++) {
    make_castle(CASTLE[f], f, false);
    make_castle(CASTLE_H[f], f, true);
  }
  static const char* I_ADD[] = {"..1..", "..1..", "11111", "..1..", "..1.."};
  static const char* I_DBL[] = {".111.", "...1.", ".111.", ".1...", ".111."};
  static const char* I_RATE[] = {"1.1..", ".1.1.", "..1.1", ".1.1.", "1.1.."};
  static const char* I_SHIELD[] = {"11111", "11111", "11111", ".111.", "..1.."};
  static const char* I_BOMB[] = {"...1.", ".111.", "11111", "11111", ".111."};
  make_tile(PK_IMG[PK_ADD], Gl, I_ADD, Wt);
  make_tile(PK_IMG[PK_DBL], Yl, I_DBL, K);
  make_tile(PK_IMG[PK_RATE], B, I_RATE, Wt);
  make_tile(PK_IMG[PK_SHIELD], V, I_SHIELD, Wt);
  make_tile(PK_IMG[PK_BOMB], R, I_BOMB, K);
  int k = 0;
  const float rings[3][2] = {{14, 12}, {17.5f, 15}, {21, 18}};
  for (auto& rg : rings)
    for (int i = 0; i < (int)rg[1]; i++) {
      float a = i / rg[1] * TAU + rg[0];
      g_slots[k++] = {C + cosf(a) * rg[0], C + sinf(a) * rg[0] + 1, fmodf(a, TAU), 0};
    }
}

bool g_prev_down = false;

}  // namespace

void game_init(uint32_t seed, float best_seconds, bool english, GameSfxCb cb) {
  g_rng = seed ? seed : 0x1234567;
  g_best = best_seconds;
  g_english = english;
  g_sfx = cb;
  if (!g_bg) g_bg = (uint8_t*)malloc(W * W);
  build_sprites();
  build_background();
  title_scene();
}

void game_step(float dt, const GameInput& in) {
  if (dt > 0.05f) dt = 0.05f;
  bool edge = in.down && !g_prev_down;
  g_prev_down = in.down;
  if (edge && (S.st == St::Title || (S.st == St::Over && S.overT > 0.8f))) {
    reset_state();
    sfx(GameSfx::Start);
  }
  if (in.down && S.st == St::Play && hypotf(in.x - C, in.y - C) > 6) S.aim = atan2f(in.y - C, in.x - C);
  if (in.shake) use_bomb();
  update(dt);
}

float game_best() { return g_best; }
bool game_take_new_best(float* seconds) {
  if (!g_new_best_pending) return false;
  g_new_best_pending = false;
  *seconds = g_best;
  return true;
}
const char* game_state_name() { return S.st == St::Title ? "title" : S.st == St::Play ? "play" : "over"; }
float game_time() { return S.t; }
int game_troops() { return S.N; }
int game_object_count() { return S.groups.n + S.arrows.n + S.pks.n + S.parts.n + S.decals.n + S.floats.n; }

void game_debug_scene(int scene) {
  switch (scene) {
    case 0: title_scene(); break;
    case 1: reset_state(); break;
    case 2: {  // heavy: boss + many groups + debris, for frame-rate measurement
      reset_state();
      S.t = 70;
      S.N = 120;
      S.boss = 99;
      S.rate = 0.07f;
      S.shield = 1;
      S.bombs = 2;
      Group* b = S.groups.add();
      *b = {-0.9f, 44, 0, 1, 2, 131, 131, true, false};
      for (int i = 0; i < 14; i++) {
        Group* g = S.groups.add();
        int n = 6 + i;
        *g = {i * 0.45f, 40.0f + (i * 7) % 36, 0, (float)i, 4, n, n, false, false};
      }
      for (int i = 0; i < 4; i++) {
        Pick* p = S.pks.add();
        *p = {i, i * 1.6f, 10, false};
      }
      splat(60, 100, 120, 14);
      splat(110, 70, 120, 12);
      for (int i = 0; i < S.decals.n; i++) S.decals.v[i].life = 99;
      break;
    }
    case 3:
      title_scene();
      S.groups.n = 0;
      S.st = St::Over;
      S.last = 95;
      S.overT = 1;
      S.N = 0;
      break;
  }
}
