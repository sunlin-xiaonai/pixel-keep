// Mac-side check for the game core: renders each debug scene at 3x into PPM files and times game_step + game_render.
// Build: see sim/build.sh
#include <chrono>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "game.h"
#include "synth.h"

static int g_sfx_count = 0;
static void on_sfx(GameSfx sfx, int param) {
  g_sfx_count++;
  synth_trigger(sfx, param);
}

static void dump(const char* path, const uint8_t* frame) {
  const int S = GAME_W * GAME_SCALE;
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 %d %d 255\n", S, S);
  std::vector<uint8_t> row(S * 3);
  for (int y = 0; y < S; y++) {
    int ly = y / GAME_SCALE, x0, x1;
    game_row_span(ly, &x0, &x1);
    for (int x = 0; x < S; x++) {
      int lx = x / GAME_SCALE;
      uint16_t c = (lx >= x0 && lx <= x1) ? GAME_PALETTE[frame[ly * GAME_W + lx]] : 0;
      row[x * 3] = ((c >> 11) & 31) * 255 / 31;
      row[x * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
      row[x * 3 + 2] = (c & 31) * 255 / 31;
    }
    fwrite(row.data(), 1, row.size(), f);
  }
  fclose(f);
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "build";
  const bool english = argc > 2 && strcmp(argv[2], "en") == 0;
  static uint8_t frame[GAME_W * GAME_W];
  static int16_t audio[800];
  const char* names[] = {"title", "play", "boss_rush", "over"};
  for (int scene = 0; scene < 4; scene++) {
    game_init(1234 + scene, 95.0f, english, on_sfx);
    game_debug_scene(scene);
    GameInput in{};
    double step_us = 0, render_us = 0;
    int max_objs = 0;
    const int frames = 600;
    for (int i = 0; i < frames; i++) {
      in.down = scene == 1 || scene == 2;
      in.x = 77 + 50 * sinf(i * 0.03f);
      in.y = 77 + 50 * cosf(i * 0.021f);
      in.shake = scene == 2 && i == 300;
      auto t0 = std::chrono::steady_clock::now();
      game_step(1.0f / 30, in);
      auto t1 = std::chrono::steady_clock::now();
      game_render(frame);
      auto t2 = std::chrono::steady_clock::now();
      step_us += std::chrono::duration<double, std::micro>(t1 - t0).count();
      render_us += std::chrono::duration<double, std::micro>(t2 - t1).count();
      if (game_object_count() > max_objs) max_objs = game_object_count();
      synth_render(audio, 800, 1.0f);
      if (i == 90 || i == 299 || i == 310 || i == frames - 1) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s_%03d.ppm", out, names[scene], i);
        dump(path, frame);
      }
    }
    printf("%-10s state=%-6s t=%5.1fs troops=%3d max_objs=%3d step=%.1fus render=%.1fus\n", names[scene],
           game_state_name(), game_time(), game_troops(), max_objs, step_us / frames, render_us / frames);
  }
  printf("sfx events: %d\n", g_sfx_count);
  return 0;
}
