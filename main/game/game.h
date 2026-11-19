#pragma once
// Pixel Keep (像素守城): platform-independent game core. Renders a 155x155 frame of palette indices;
// the board firmware (main_game.cpp) and the Mac simulator (sim/game_sim.cpp) scale it 3x to the round screen.
#include <stdint.h>

constexpr int GAME_W = 155;
constexpr int GAME_SCALE = 3;  // 155 * 3 = 465 px of the 466 px panel

// Tikpal Voice palette as RGB565, index order: k n m g o d l w r y Y G b v p s
extern const uint16_t GAME_PALETTE[16];

struct GameInput {
  bool down;   // finger on the screen
  float x, y;  // logical pixels (0..155), valid while down
  bool shake;  // one shake gesture detected since the last step
};

enum class GameSfx : uint8_t { Hit, Pop, Pick, Castle, Boss, Praise, Bomb, Over, Start, Empty };
typedef void (*GameSfxCb)(GameSfx sfx, int param);

void game_init(uint32_t seed, float best_seconds, bool english, GameSfxCb sfx);
void game_step(float dt, const GameInput& in);
void game_render(uint8_t* frame);  // GAME_W * GAME_W indices
// Left/right inclusive edges of the visible circle on each logical row (x1 < x0: empty row).
void game_row_span(int y, int* x0, int* x1);

float game_best();
bool game_take_new_best(float* seconds);  // true once after a run beats the stored record
const char* game_state_name();
float game_time();
int game_troops();
int game_object_count();
// Debug scenes for measuring: 0 title, 1 play, 2 boss rush (heavy), 3 game over
void game_debug_scene(int scene);
