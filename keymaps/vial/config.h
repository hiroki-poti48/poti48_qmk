/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once
#define VIAL_KEYBOARD_UID {0x98, 0xCC, 0x69, 0x3A, 0x4B, 0x2D, 0xDF, 0xEA}
#define VIAL_UNLOCK_COMBO_ROWS {0, 5}
#define VIAL_UNLOCK_COMBO_COLS {0, 5}

// VialのDynamic Keymapマクロの枠数を拡張（既定16 → 64）
#define DYNAMIC_KEYMAP_MACRO_COUNT 255

// Dynamic keymap レイヤー数を最大化: QMK のレイヤー状態は最大32ビットなので
// 実用上の上限は 32 レイヤーです。これ以上にするとレイヤービットマスクで扱えなくなります。
#ifdef DYNAMIC_KEYMAP_LAYER_COUNT
#    undef DYNAMIC_KEYMAP_LAYER_COUNT
#endif
#define DYNAMIC_KEYMAP_LAYER_COUNT 32

// Vial の可変エントリ数を増やす（利用可能な EEPROM に依存します）。
// 各値は uint8_t インデックスで扱われるため 255 未満に留めます。
// ビルド時の EEPROM 容量に合わせて安全側の値にする（必要ならここを増やして下さい）
#ifndef VIAL_TAP_DANCE_ENTRIES
#define VIAL_TAP_DANCE_ENTRIES 64
#endif
#ifndef VIAL_COMBO_ENTRIES
#define VIAL_COMBO_ENTRIES 128
#endif
#ifndef VIAL_KEY_OVERRIDE_ENTRIES
#define VIAL_KEY_OVERRIDE_ENTRIES 128
#endif
#ifndef VIAL_ALT_REPEAT_KEY_ENTRIES
#define VIAL_ALT_REPEAT_KEY_ENTRIES 64
#endif

/* RP2040 wear-leveling configuration to increase logical EEPROM available
 * NOTE: Increasing logical EEPROM increases RAM usage (logical size in bytes).
 * Adjust these values according to your board's RAM/flash availability.
 */
#ifndef WEAR_LEVELING_BACKING_SIZE
/* Backing size (bytes) used by the wear-leveling driver. Use 128KB backing
 * so that logical EEPROM can be up to 64KB. Must be <= physical flash size.
 */
#define WEAR_LEVELING_BACKING_SIZE 131072
#endif

#ifndef WEAR_LEVELING_LOGICAL_SIZE
/* QMK EEPROM subsystem max logical size is 64KB. Set to the maximum. */
#define WEAR_LEVELING_LOGICAL_SIZE 65536
#endif

// RP2040 フラッシュサイズ (実機に合わせて変更)
// 4MB: (4 * 1024 * 1024)
// 16MB: (16 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#    define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif

// 秘密ストレージ領域サイズ (後で10MB等に変更可能)
// 4MB運用: 1MB推奨
// 16MB運用: 10MBなど
#ifndef SECRET_STORAGE_SIZE
#    define SECRET_STORAGE_SIZE (1 * 1024 * 1024)
#endif

// ポーリング頻度を抑えて左側行読み取り遅延を軽減（分解能と負荷のバランス）
#define POINTING_DEVICE_TASK_THROTTLE_MS 10

// スレーブ側(右側)にトラックポイントがある場合
#define POINTING_DEVICE_RIGHT

// マウスキー設定はkeyboards/poti48/config.hで定義済み

// PS/2設定(右側のみ実際に接続)
#define PS2_CLOCK_PIN GP9
#define PS2_DATA_PIN GP8

#define MIDI_ADVANCED

// ユーザー設定EEPROM領域サイズを定義 (バイト)
// 構造体が33バイト以上になる可能性があるため、余裕を持って64バイト確保
#define EECONFIG_USER_DATA_SIZE 64

// === スクロール調整（追記） ===
// 高解像度スクロール: 1ティックを1/120ノッチとして送る（ThinkPad本体と同じ方式）
#define POINTING_DEVICE_HIRES_SCROLL_ENABLE
// ホイールの値を16bitに拡張（hi-res で値が大きくなるため必須）
#define WHEEL_EXTENDED_REPORT
