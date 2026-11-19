// 像素守城 Pixel Keep: a relaxed pixel-art defence game for the round screen. Drag to aim the archers in the
// castle, pick up power-ups, and shake the device (or tap the side key) to drop a bomb. A double click is the
// system's: the app ring, which is how one leaves; a hold asks 「回到 Pip 说？」.
//
// The order every extension firmware follows: NVS -> yaykit_app -> screen -> speaker -> side key -> motion ->
// Yaykit Link -> first frame -> console -> yk_app_ready(). The game then draws straight to the panel
// (board_direct.h) from its own task on core 1; whenever something of the system's is up (the app ring,
// 「回到 Pip 说？」, the hint, the pull-down, the code), the game pauses and gives the panel back to LVGL, which
// shows the paused frame under it.
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_direct.h"
#include "cjk_font.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pk_core.h"
#include "yaykit_app.h"
#include "yaykit_key.h"
#include "yaykit_link.h"
#include "yaykit_motion.h"
#include "yaykit_speak.h"
#include "yaykit_ui.h"
#include "yk_apps.h"
#include "yk_hint.h"
#include "yk_vc_view.h"

#define APP_SLUG "pixel-keep"
#define NVS_NS "yk_pixelkeep"
#define PIXELS (PK_SCREEN * PK_SCREEN)
#define PK_TOUCH_TOP 46
static const char *TAG = "pixel-keep";

static uint16_t *s_be;        // the panel's frame, big-endian RGB565, PSRAM
static uint16_t *s_le;        // the same for LVGL while paused (the backdrop), PSRAM
static lv_image_dsc_t s_dsc;  // s_le as an LVGL image
static lv_obj_t *s_screen, *s_backdrop;
static uint32_t s_screen_base;  // children of s_screen that are ours (the backdrop)

static volatile bool s_bomb;  // the side key or a shake, taken by the game task
static bool s_sound;

// Shown by pixelkeep.status and kept for the summary.
static volatile float s_fps;
static int s_runs;
static float s_last_run;

// ---------------------------------------------------------------- input from other tasks

/** Through yaykit_app: a tap drops a bomb. The hold is left unused ( 「回到 Pip 说？」), the double click to the app
 *  ring. Only a flag here: the game task acts on it. */
static bool on_key(const yk_key_event_t *ev, void *ctx) {
    (void)ctx;
    if (yk_vc_on_key(ev)) return true;
    if (ev->type == YK_KEY_TAP) {
        s_bomb = true;
        return true;
    }
    return false;
}

static void on_motion(const yk_motion_event_t *ev, void *ctx) {
    (void)ctx;
    if (ev->kind == YK_MOTION_SHAKE_START) s_bomb = true;
}

// ---------------------------------------------------------------- sound: one synth voice for the whole game

static bool synth_block(float *out, size_t n, const yk_sfx_block_t *blk, void *ctx) {
    (void)blk, (void)ctx;
    pk_audio(out, n, 1.0f);
    return true;
}

static void sound_start(void) {
    if (yk_speak_init_cfg(&(yk_speak_config_t){.rate = PK_SYNTH_RATE, .block_ms = 5}) != ESP_OK) {
        ESP_LOGE(TAG, "no speaker; playing silent");
        return;
    }
    s_sound = yk_sfx_play_synth(synth_block, NULL, &(yk_sfx_opts_t){.gain = 0.5f, .no_duck = true}) > 0;
}

// ---------------------------------------------------------------- the best time

static float best_load(void) {
    nvs_handle_t h;
    uint32_t ms = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "best_ms", &ms);
        nvs_close(h);
    }
    return ms / 1000.0f;
}

static void best_save(float seconds) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "best_ms", (uint32_t)(seconds * 1000.0f));
    nvs_commit(h);
    nvs_close(h);
}

static void fmt_time(float seconds, char *out, size_t n) {
    unsigned s = seconds > 0 ? (unsigned)seconds : 0;
    if (s > 99 * 60 + 59) s = 99 * 60 + 59;
    snprintf(out, n, "%u:%02u", s / 60, s % 60);
}

// ---------------------------------------------------------------- the game task

static void frame_to_le(void) {
    for (int i = 0; i < PIXELS; i++) s_le[i] = (uint16_t)((s_be[i] >> 8) | (s_be[i] << 8));
}

/** What of the system's LVGL draws now (NULL: nothing, the game may draw). The ring, 「回到 Pip 说？」, the hint
 *  and the pull-down are made on the active screen; another screen is the battery page. LVGL's top layer is left
 *  out: the pull-down's invisible edge strip lives there all the time. Under board_lock(). */
static const char *overlay_up(void) {
    if (board_display_asleep()) return "asleep";
    if (lv_screen_active() != s_screen) return "screen";
    if (yk_apps_is_open()) return "ring";
    if (yk_apps_ask_is_open()) return "ask";
    if (yk_hint_is_shown()) return "hint";
    if (yk_vc_showing()) return "code";
    const uint32_t n = lv_obj_get_child_count(s_screen);
    for (uint32_t i = s_screen_base; i < n; i++)
        if (!lv_obj_is_hidden(lv_obj_get_child(s_screen, (int32_t)i))) return "sheet";
    return NULL;
}

typedef struct {
    int frames;
    int64_t work_us, blit_us, worst_us;
    int64_t since;
} stats_t;

static void stats_report(stats_t *st, int64_t now) {
    const float secs = (now - st->since) / 1e6f;
    if (secs < 5.0f) return;
    s_fps = st->frames / secs;
    if (st->frames)
        printf("PIXELKEEP_FPS fps=%.1f work_ms=%.2f blit_ms=%.2f worst_ms=%.1f state=%s objs=%d internal_min=%u\n",
               s_fps, st->work_us / 1000.0f / st->frames, st->blit_us / 1000.0f / st->frames, st->worst_us / 1000.0f,
               pk_state(), pk_objects(), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    *st = (stats_t){.since = now};
}

static void on_state(const char *was, const char *now) {
    const bool playing = !strcmp(now, "play");
    printf("PIXELKEEP_STATE %s->%s\n", was, now);
    yk_app_keep_screen_on(playing);
    if (s_sound) yk_speak_hold_amp(playing);
    if (!strcmp(was, "play") && !strcmp(now, "over")) {
        s_runs++;
        s_last_run = pk_time();
        float best;
        if (pk_take_new_best(&best)) best_save(best);
        printf("PIXELKEEP_RUN n=%d seconds=%.1f best=%.1f\n", s_runs, s_last_run, pk_best());
    }
}

static void game_task(void *arg) {
    (void)arg;
    if (board_touch_fast_start(5) != ESP_OK) ESP_LOGE(TAG, "no fast touch");
    bool down = false, paused = true;
    int64_t clear_since = 0;  // the system's things gone since then; the game resumes 200 ms later
    float tx = 0, ty = 0;
    const char *state = pk_state();
    stats_t st = {.since = esp_timer_get_time()};
    int64_t last = esp_timer_get_time();
    for (;;) {
        board_lock(-1);
        const char *why = overlay_up();
        const int64_t now_us = esp_timer_get_time();
        if (!why && paused && !clear_since) clear_since = now_us;
        if (why || (paused && now_us - clear_since < 200000)) {
            if (why) clear_since = 0;
            if (board_direct_active()) {
                frame_to_le();
                lv_obj_invalidate(s_backdrop);
                board_direct_end();
            }
            board_unlock();
            if (!paused) {
                yk_app_keep_screen_on(false);
                printf("PIXELKEEP_PAUSE why=%s state=%s\n", why, pk_state());
            }
            paused = true;
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        if (paused) {
            clear_since = 0;
            board_direct_begin();
            board_touch_fast_clear();
            down = false;
            s_bomb = false;
            paused = false;
            last = esp_timer_get_time();
            st = (stats_t){.since = last};
            yk_app_keep_screen_on(!strcmp(state, "play"));
            printf("PIXELKEEP_RESUME state=%s\n", state);
        }
        lv_indev_t *indev = board_touch_indev();
        lv_point_t point = {0, 0};
        const bool lv_down = indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
        if (lv_down) lv_indev_get_point(indev, &point);
        board_unlock();

        // A press and its release within one frame still reach the game as a press.
        bool pressed = false;
        board_touch_edge_t edge;
        while (board_touch_fast_take(&edge)) {
            down = edge.pressed && edge.y >= PK_TOUCH_TOP;
            if (down) {
                tx = edge.x;
                ty = edge.y;
                pressed = true;
            }
        }
        if (pressed) yk_app_input();
        if (down && lv_down) {
            tx = point.x;
            ty = point.y;
        }
        const pk_input_t in = {.down = down || pressed, .x = tx, .y = ty, .bomb = s_bomb};
        s_bomb = false;

        const int64_t t0 = esp_timer_get_time();
        pk_step((t0 - last) / 1e6f, &in);
        last = t0;
        const char *now = pk_state();
        if (strcmp(now, state)) {
            on_state(state, now);
            state = now;
        }
        pk_render_be(s_be);
        const int64_t t1 = esp_timer_get_time();
        board_lock(-1);
        if (board_direct_active()) board_direct_blit(s_be, 0, 0, 0, PK_SCREEN, PK_SCREEN);
        board_unlock();
        const int64_t t2 = esp_timer_get_time();
        st.frames++;
        st.work_us += t1 - t0;
        st.blit_us += t2 - t1;
        if (t2 - t0 > st.worst_us) st.worst_us = t2 - t0;
        stats_report(&st, t2);
        // At most 40 frames a second, and at least 2 ms between frames for LVGL and the system to take the lock.
        const int64_t left_us = 25000 - (esp_timer_get_time() - t0);
        vTaskDelay(pdMS_TO_TICKS(left_us > 2000 ? left_us / 1000 : 2));
    }
}

// ---------------------------------------------------------------- leaving, commands, console

/** Back to the main firmware: the sound fades and Tikpal gets the summary. Kept to writing (YK_APP_LEAVE_MS). */
static void on_leave(void *ctx) {
    (void)ctx;
    yk_app_keep_screen_on(false);
    if (s_sound) yk_sfx_stop_all(120);
    if (!s_runs) return;
    char last[16], best[16], runs[12], sentence[48], data[96];
    fmt_time(s_last_run, last, sizeof(last));
    fmt_time(pk_best(), best, sizeof(best));
    snprintf(runs, sizeof(runs), "%d", s_runs);
    yk_summary_t summary;
    yk_summary_init(&summary);
    const bool english = yk_app_english();
    yk_summary_stat(&summary, last, english ? "Last" : "这局");
    yk_summary_stat(&summary, best, english ? "Best" : "最久");
    yk_summary_stat(&summary, runs, english ? "Runs" : "局");
    snprintf(sentence, sizeof(sentence), english ? "Held the castle for %s" : "守住了 %s", last);
    snprintf(data, sizeof(data), "{\"runs\":%d,\"lastSeconds\":%d,\"bestSeconds\":%d}", s_runs, (int)s_last_run,
             (int)pk_best());
    cJSON *doc = cJSON_Parse(data);
    yk_app_set_result_summary(sentence, &summary, doc);
    cJSON_Delete(doc);
}

static void status_json(cJSON *reply) {
    cJSON_AddStringToObject(reply, "state", pk_state());
    cJSON_AddNumberToObject(reply, "seconds", pk_time());
    cJSON_AddNumberToObject(reply, "best", pk_best());
    cJSON_AddNumberToObject(reply, "troops", pk_troops());
    cJSON_AddNumberToObject(reply, "objects", pk_objects());
    cJSON_AddNumberToObject(reply, "fps", s_fps);
    cJSON_AddNumberToObject(reply, "runs", s_runs);
    cJSON_AddNumberToObject(reply, "sfx", pk_sfx_count());
    cJSON_AddBoolToObject(reply, "sound", s_sound);
}

static esp_err_t on_command(const char *name, const cJSON *args, cJSON *reply, const char **code, void *ctx) {
    (void)ctx;
    yk_app_input();
    esp_err_t err = yk_app_command(name, args, reply, code);  // app.exit, app.recovery, app.status
    if (err != ESP_ERR_NOT_SUPPORTED) return err;
    if (!strcmp(name, "pixelkeep.status")) {
        status_json(reply);
        return ESP_OK;
    }
    *code = "UNKNOWN_COMMAND";
    return ESP_ERR_NOT_SUPPORTED;
}

static int status_cmd(int argc, char **argv) {
    (void)argc, (void)argv;
    printf("PIXELKEEP_STATUS state=%s seconds=%.1f best=%.1f troops=%d objects=%d fps=%.1f runs=%d sfx=%d sound=%d\n",
           pk_state(), pk_time(), pk_best(), pk_troops(), pk_objects(), s_fps, s_runs, pk_sfx_count(), s_sound);
    return 0;
}

static int heap_cmd(int argc, char **argv) {
    (void)argc, (void)argv;
    printf("HEAP internal=%u internal_min=%u psram=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static void start_console(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = APP_SLUG ">";
    cfg.task_stack_size = 8192;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &cfg, &repl));
    esp_console_register_help_command();
    ESP_ERROR_CHECK(yk_link_register_console());
    ESP_ERROR_CHECK(yk_app_register_console());
    ESP_ERROR_CHECK(yk_speak_register_console());
    ESP_ERROR_CHECK(yk_motion_register_console());
    yk_ui_register_snapshot();
    const esp_console_cmd_t heap = {.command = "heap", .help = "available memory", .func = heap_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap));
    const esp_console_cmd_t status = {.command = "pixelkeep", .help = "game state and frame rate", .func = status_cmd};
    ESP_ERROR_CHECK(esp_console_cmd_register(&status));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void app_main(void) {
    // 1. NVS: never erased (shared with every app; holds the Wi-Fi credentials).
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS unavailable (%s); running without saved settings", esp_err_to_name(err));
    // 2. yaykit_app first: the crash loop guard, and the double click home.
    ESP_ERROR_CHECK(yk_app_start(&(yk_app_config_t){.slug = APP_SLUG, .version = APP_VERSION}));
    yk_app_on_leave(on_leave, NULL);
    // 3. Screen (dark until the first frame), speaker, side key (delay_tap: the first press of a double click is
    //    not a tap), motion sensor.
    if (!board_display_start_opts(&(board_display_opts_t){.start_dark = true})) {
        ESP_LOGE(TAG, "display failed");
        return;
    }
    sound_start();
    ESP_ERROR_CHECK(yk_key_init(&(yk_key_config_t){.delay_tap = true}));
    ESP_ERROR_CHECK(yk_app_on_key(on_key, NULL));
    if (yk_motion_start(&(yk_motion_config_t){.on_event = on_motion}) != ESP_OK) ESP_LOGE(TAG, "no motion sensor");
    // 4. Yaykit Link: `yk info` and `yk cmd` over USB.
    const yk_link_config_t link = {.board = "tikpal-r1", .app_slug = APP_SLUG, .app_version = APP_VERSION,
                                   .layout = "yaykit-v1", .idle_seconds = 90, .on_command = on_command};
    ESP_ERROR_CHECK(yk_link_init(&link));
    yk_launch_t launch;
    if (yk_app_launch(&launch) == ESP_OK && launch.present) printf("PIXELKEEP_LAUNCH intent=%s\n", launch.intent);

    // 5. The frames in PSRAM and the title screen as LVGL's first frame.
    s_be = heap_caps_malloc(PIXELS * 2, MALLOC_CAP_SPIRAM);
    s_le = heap_caps_malloc(PIXELS * 2, MALLOC_CAP_SPIRAM);
    if (!s_be || !s_le || !pk_init(esp_random(), best_load(), yk_app_english())) {
        ESP_LOGE(TAG, "no memory for the frames");
        return;
    }
    pk_render_be(s_be);
    frame_to_le();
    s_dsc = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                                        .w = PK_SCREEN, .h = PK_SCREEN, .stride = PK_SCREEN * 2},
                             .data_size = PIXELS * 2, .data = (const uint8_t *)s_le};
    board_lock(-1);
    ESP_ERROR_CHECK(cjk_font_init());
    s_screen = lv_obj_create(NULL);
    lv_obj_set_scrollable(s_screen, false);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    s_backdrop = lv_image_create(s_screen);
    lv_image_set_src(s_backdrop, &s_dsc);
    lv_obj_set_pos(s_backdrop, 0, 0);
    s_screen_base = lv_obj_get_child_count(s_screen);
    lv_screen_load(s_screen);
    lv_refr_now(NULL);
    board_brightness_set(yk_app_brightness());
    // The first time in this app: the arrow to the side key, 「双击：应用和回家」 (4 s; the game waits under it).
    if (yk_hint_should(YK_HINT_APPS)) yk_hint_show(s_screen, YK_HINT_APPS, NULL);
    board_unlock();
    // 6. The console, then yk_app_ready() (its banner tells tools the console listens), then the game.
    start_console();
    yk_app_ready();
    xTaskCreatePinnedToCore(game_task, "pixelkeep", 6144, NULL, 4, NULL, 1);
}
