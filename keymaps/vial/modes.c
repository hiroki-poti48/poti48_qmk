/* modes.c
 * Pointing device task and matrix scan user hooks.
 * Separated from ps2_split.c to keep PS/2 bridge logic independent.
 * Handles: scroll mode, precise/fast mode, gesture integration, speed multiplier.
 */
#include QMK_KEYBOARD_H
#include <hardware/gpio.h>
#include "gesture.h"
#include "local_settings.h"
#include "mousekey.h"

// 押しながらマウス移動をスクロールに変換するフラグ
static bool scroll_mode = false;

// スクロールモード用の累積値
static int16_t scroll_accum_x = 0;
static int16_t scroll_accum_y = 0;
static uint8_t scroll_lock_axis = 0; // 0=未決定, 1=垂直, 2=水平

// レイヤーベースのスクロール用
static int16_t layer_scroll_accum_x = 0;
static int16_t layer_scroll_accum_y = 0;
static uint8_t layer_scroll_lock_axis = 0;

// 押しながら速度調整フラグ
static bool precise_mode_hold = false;
static bool fast_mode_hold = false;

// layer masks (local_settings.h で extern 宣言済み)
uint32_t scroll_layer_mask = (1UL << 3) | (1UL << 8);
uint32_t precise_layer_mask = 0;
uint32_t fast_layer_mask = 0;
uint32_t gesture_move_layer_mask = 0;
uint32_t gesture_static_layer_mask = 0;

// Rapid Fire (keymap.c から参照)
extern bool rapid_fire_enabled;
extern uint16_t rapid_fire_timer;
#define RAPID_FIRE_INTERVAL 10

// === Scroll mode ===
void scroll_start(uint16_t keycode) {
    (void)keycode;
    scroll_mode = true;
    scroll_lock_axis = 0;
}

void scroll_end(void) {
    scroll_mode = false;
    scroll_accum_x = 0;
    scroll_accum_y = 0;
    scroll_lock_axis = 0;
}

bool scroll_is_active(void) {
    return scroll_mode;
}

void scroll_layer_toggle(uint8_t layer) {
    if (layer < 32) {
        scroll_layer_mask ^= (1UL << layer);
        save_user_config();
    }
}

bool scroll_layer_is_set(uint8_t layer) {
    return (layer < 32) && (scroll_layer_mask & (1UL << layer));
}

// === Precise mode ===
void precise_layer_toggle(uint8_t layer) {
    if (layer < 32) {
        precise_layer_mask ^= (1UL << layer);
        save_user_config();
    }
}

bool precise_layer_is_set(uint8_t layer) {
    return (layer < 32) && (precise_layer_mask & (1UL << layer));
}

void precise_mode_start(void) {
    precise_mode_hold = true;
}

void precise_mode_end(void) {
    precise_mode_hold = false;
}

// === Fast mode ===
void fast_layer_toggle(uint8_t layer) {
    if (layer < 32) {
        fast_layer_mask ^= (1UL << layer);
        save_user_config();
    }
}

bool fast_layer_is_set(uint8_t layer) {
    return (layer < 32) && (fast_layer_mask & (1UL << layer));
}

void fast_mode_start(void) {
    fast_mode_hold = true;
}

void fast_mode_end(void) {
    fast_mode_hold = false;
}

// === Gesture layer ===
void gesture_move_layer_toggle(uint8_t layer) {
    if (layer < 32) {
        gesture_move_layer_mask ^= (1UL << layer);
        save_user_config();
    }
}

bool gesture_move_layer_is_set(uint8_t layer) {
    return (layer < 32) && (gesture_move_layer_mask & (1UL << layer));
}

void gesture_static_layer_toggle(uint8_t layer) {
    if (layer < 32) {
        gesture_static_layer_mask ^= (1UL << layer);
        save_user_config();
    }
}

bool gesture_static_layer_is_set(uint8_t layer) {
    return (layer < 32) && (gesture_static_layer_mask & (1UL << layer));
}

// === 加速カーブ ===
// トラックポイントを動かす速さ(velocity)に応じて倍率を変える。
// ゆっくり動かした時は控えめな倍率、素早く動かした時だけ大きな倍率にすることで、
// 「細かい制御」と「ピーク速度」の間の中間域を広く持たせる。
static int16_t scroll_accel_multiplier(int16_t velocity) {
    const int16_t MULT_MIN     = 4;   // ゆっくり動かした時の倍率(小さいほど細かい)
    const int16_t MULT_MAX     = 8;   // 素早く動かした時の倍率(ピーク時の速さ)
    // この速さでMULT_MAXに到達(大きいほど中間域が広い)。
    // Windowsは広い中間域が滑らかさに繋がるが、Macはノッチ単位でしか表現できないため、
    // 同じ「控えめな中間域」がそのまま「反応が鈍い/固い」に直結してしまう。
    // Macの時だけ早めにフル倍率へ到達させ、その固さを和らげる。
    const int16_t VELOCITY_MAX = (os_mode == 1) ? 15 : 45;

    int32_t v = velocity;
    if (v < 0) v = -v;
    if (v > VELOCITY_MAX) v = VELOCITY_MAX;

    int32_t range = MULT_MAX - MULT_MIN;
    int32_t mult  = MULT_MIN + (range * v * v) / ((int32_t)VELOCITY_MAX * VELOCITY_MAX);
    return (int16_t)mult;
}

// === Scroll conversion helper ===
// 戻り値は「高解像度スクロールのティック」単位(120ティック=1ノッチ)
static mouse_hv_report_t accumulate_scroll(int16_t *accum, int16_t input, int div, int mult) {
    *accum += input;
    if (*accum >= div || *accum <= -div) {
        int32_t wheel = (int32_t)(*accum / div) * mult;
        *accum = *accum % div;
        if (wheel > MOUSE_REPORT_HV_MAX) wheel = MOUSE_REPORT_HV_MAX;
        if (wheel < MOUSE_REPORT_HV_MIN) wheel = MOUSE_REPORT_HV_MIN;
        return (mouse_hv_report_t)wheel;
    }
    return 0;
}

// === Mac用: 高解像度ティックを「ノッチ単位」に変換する ===
// macOSはUSB HIDの「Resolution Multiplier」を尊重せず、
// 送られてきた値をそのまま「ノッチ数」として扱ってしまう。
// そのため、Windowsと同じ細かいティック(120=1ノッチ)をそのまま送ると、
// 実際のノッチ数の120倍のスクロールをした扱いになり、爆速になる。
// ここでは端数を蓄積しておき、実際に1ノッチ分たまった時だけ
// 「ノッチ数そのもの(1,2,3...)」を送るようにする。
static int16_t mac_notch_accum_v = 0;
static int16_t mac_notch_accum_h = 0;

static mouse_hv_report_t mac_notch_convert(int16_t *accum, mouse_hv_report_t hires_ticks) {
    const int16_t TICKS_PER_NOTCH = 120;
    int32_t total = (int32_t)(*accum) + (int32_t)hires_ticks;
    int32_t notches = total / TICKS_PER_NOTCH;
    *accum = (int16_t)(total % TICKS_PER_NOTCH);
    if (notches > MOUSE_REPORT_HV_MAX) notches = MOUSE_REPORT_HV_MAX;
    if (notches < MOUSE_REPORT_HV_MIN) notches = MOUSE_REPORT_HV_MIN;
    return (mouse_hv_report_t)notches;
}

static void do_scroll(report_mouse_t *rpt, int16_t *ax, int16_t *ay, uint8_t *lock,
                      int16_t ix, int16_t iy) {
    const int SCROLL_DIV = 3;
    const int AXIS_SWITCH = 5;
    const int AXIS_START = 1;

    *ax += ix;
    *ay += iy;

    int16_t abs_x = *ax > 0 ? *ax : -*ax;
    int16_t abs_y = *ay > 0 ? *ay : -*ay;

    if (*lock == 0) {
        if (abs_y >= AXIS_START || abs_x >= AXIS_START) {
            *lock = (abs_y > abs_x) ? 1 : 2;
            // Mac: スクロールを新しく始めた瞬間だけ、ノッチ変換の蓄積に
            // 「助走」を一度だけ与えて、初動の重さを軽減する。
            // (継続して動かしている間の速さには影響しない)
            if (os_mode == 1) {
                const int16_t PRIME_TICKS = 60; // 半ノッチ分
                if (*lock == 1) mac_notch_accum_v = PRIME_TICKS;
                else mac_notch_accum_h = PRIME_TICKS;
            }
        }
    } else {
        if (*lock == 1 && abs_x > AXIS_SWITCH) {
            *lock = 2;
            *ay = 0;
        }
        if (*lock == 2 && abs_y > AXIS_SWITCH) {
            *lock = 1;
            *ax = 0;
        }
    }

    // その瞬間の動かす速さ(このtickでの生の移動量)を元に倍率を都度計算する
    mouse_hv_report_t wheel_v = 0, wheel_h = 0;
    if (*lock == 1) wheel_v = accumulate_scroll(ay, 0, SCROLL_DIV, scroll_accel_multiplier(iy));
    if (*lock == 2) wheel_h = accumulate_scroll(ax, 0, SCROLL_DIV, scroll_accel_multiplier(ix));

    // Macの時だけ、ノッチ単位に変換してから送る
    if (os_mode == 1) { // 1 = Mac
        wheel_v = mac_notch_convert(&mac_notch_accum_v, wheel_v);
        wheel_h = mac_notch_convert(&mac_notch_accum_h, wheel_h);
    }

    rpt->x = 0; rpt->y = 0;
    // 垂直ホイール: Windowsは反転(下方向(y+)がスクロール下になるようHIDでは負にする)。
    // Mac(ナチュラルスクロールON)は実測の結果、反転させない方が正しい向きになる。
    rpt->v = (os_mode == 1) ? wheel_v : -wheel_v;
    // 水平ホイール: Windowsは反転しない。Mac(ナチュラルスクロールON)は実測の結果、反転させる。
    rpt->h = (os_mode == 1) ? -wheel_h : wheel_h;
}

// === Main pointing device hook ===
report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (!is_keyboard_master()) {
        return mouse_report;
    }

    // マウスキーのボタン状態をマージしてドラッグの瞬断を防ぐ
    mouse_report.buttons |= mousekey_get_report().buttons;

    // Scroll mode (hold)
    if (scroll_mode) {
        do_scroll(&mouse_report, &scroll_accum_x, &scroll_accum_y, &scroll_lock_axis,
                  mouse_report.x, mouse_report.y);
        return mouse_report;
    }

    // Scroll layer (layer-based)
    {
        uint8_t active_layer = biton32(layer_state);
        if (scroll_layer_mask & (1UL << active_layer)) {
            do_scroll(&mouse_report, &layer_scroll_accum_x, &layer_scroll_accum_y, &layer_scroll_lock_axis,
                      mouse_report.x, mouse_report.y);
            return mouse_report;
        } else {
            layer_scroll_accum_x = 0;
            layer_scroll_accum_y = 0;
            layer_scroll_lock_axis = 0;
        }
    }

    // ジェスチャ記録
    gesture_record_movement(mouse_report.x, mouse_report.y);

    // ジェスチャ中はカーソル抑制
    if (gesture_should_suppress_cursor()) {
        mouse_report.x = 0;
        mouse_report.y = 0;
        return mouse_report;
    }

    // 速度倍率適用 (0.25x〜5.0x)
    if (mouse_report.x != 0 || mouse_report.y != 0) {
        uint8_t active_layer = biton32(layer_state);
        bool is_precise = precise_mode_hold || (precise_layer_mask & (1UL << active_layer));
        bool is_fast    = fast_mode_hold    || (fast_layer_mask    & (1UL << active_layer));

        uint16_t mult_q = QS_mouse_speed_mult;
        if (mult_q < 1)  mult_q = 4;
        if (mult_q > 20) mult_q = 20;

        if (is_precise) {
            mult_q = mult_q / 4;
            if (mult_q < 1) mult_q = 1;
        } else if (is_fast) {
            mult_q = mult_q * 2;
            if (mult_q > 20) mult_q = 20;
        }

        int32_t sx = (int32_t)mouse_report.x * mult_q;
        int32_t sy = (int32_t)mouse_report.y * mult_q;
        int16_t nx = (int16_t)(sx / 4);
        int16_t ny = (int16_t)(sy / 4);

        if (nx > 127) nx = 127;
        if (nx < -127) nx = -127;
        if (ny > 127) ny = 127;
        if (ny < -127) ny = -127;
        mouse_report.x = (int8_t)nx;
        mouse_report.y = (int8_t)ny;
    }

    return mouse_report;
}

void matrix_init_user(void) {
    // デバッグLED (GP17): master=HIGH, slave=LOW
    gpio_init(17);
    gpio_set_dir(17, GPIO_OUT);
}

void matrix_scan_user(void) {
    static uint8_t last_layer = 0;
    uint8_t current_layer = biton32(layer_state);

    // レイヤー変更時のジェスチャ自動起動/終了
    if (current_layer != last_layer) {
        bool was_mgst = (gesture_move_layer_mask & (1UL << last_layer)) != 0;
        bool was_sgst = (gesture_static_layer_mask & (1UL << last_layer)) != 0;
        if ((was_mgst || was_sgst) && gesture_is_active()) {
            gesture_end();
        }

        bool is_mgst = (gesture_move_layer_mask & (1UL << current_layer)) != 0;
        bool is_sgst = (gesture_static_layer_mask & (1UL << current_layer)) != 0;
        if (is_mgst) {
            gesture_start(0, true);
        } else if (is_sgst) {
            gesture_start(0, false);
        }

        last_layer = current_layer;
    }

    // ジェスチャタイムアウト
    gesture_check_timeout();

    // Rapid Fire (auto-click)
    if (is_keyboard_master() && rapid_fire_enabled) {
        if (timer_elapsed(rapid_fire_timer) > RAPID_FIRE_INTERVAL) {
            rapid_fire_timer = timer_read();
            static bool click = false;
            report_mouse_t r = pointing_device_get_report();
            if (click) r.buttons &= ~MOUSE_BTN1;
            else       r.buttons |=  MOUSE_BTN1;
            click = !click;
            pointing_device_set_report(r);
            pointing_device_send();
        }
    }
}
