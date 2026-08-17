#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
// HARDWARE: confirmed from the LCDWIKI 2.4" MAR2406 user manual
// =====================================================================
// Parallel 8-bit shield, driver IC ILI9341, plugs directly onto the Mega
// header. Library stack: LCDWIKI_GUI + LCDWIKI_KBV.
//   LCD_D0..D7 -> D8, D9, D2, D3, D4, D5, D6, D7  (fixed inside the lib)
//   LCD_RST -> A4   LCD_CS -> A3   LCD_RS(CD) -> A2
//   LCD_WR  -> A1   LCD_RD -> A0
//   SD_SS->D10  SD_DI->D11  SD_DO->D12  SD_SCK->D13  (unused by this game)
// Constructor: LCDWIKI_KBV mylcd(ILI9341, A3, A2, A1, A0, A4);
//
// Shield occupies D2-D13 and A0-A4, so game inputs use the remaining
// free pins (D22+ and A6-A15).
#define PIN_JOY_X       A8
#define PIN_JOY_Y       A9      // read but unused (vertical movement not in spec)

#define PIN_BTN_BLASTER 22      // Button 1: primary weapon
#define PIN_BTN_CLEAVER 23      // Button 2: special 1
#define PIN_BTN_SHIELD  24      // Button 3: special 2
#define PIN_BTN_BOMB    25      // Button 4: ultimate

// Touch is not wired into this game (joystick + 4 buttons drive
// everything). The MAR2406's resistive touch shares the LCD bus rather
// than separate XP/YP/XM/YM pins, so it's out of scope for now.
#define TOUCH_ENABLED   0

// =====================================================================
// SCREEN & COLORS
// =====================================================================
#define SCREEN_W 320
#define SCREEN_H 240
#define COLOR_BG        0x0000   // black
#define COLOR_PLAYER    0x07E0   // green hull
#define COLOR_WING      0x03E0   // darker green wings
#define COLOR_COCKPIT   0x07FF   // cyan canopy
#define COLOR_ENGINE_A  0xFFE0   // engine flicker colour A (yellow)
#define COLOR_ENGINE_B  0xFD20   // engine flicker colour B (orange)
#define COLOR_LASER     0x07FF   // cyan
#define COLOR_CLEAVER   0xFFE0   // yellow
#define COLOR_CHICKEN_FLOCK 0xFD20  // orange -- regular flying chickens
#define COLOR_CHICKEN_ELITE 0x8410  // grey/armoured -- stationary wall
#define COLOR_VEST      0x22B5   // blue armored vest (elites + boss)
#define COLOR_VEST_DK   0x1188   // darker blue vest shading
#define COLOR_COMB      0xF800   // red comb/wattle
#define COLOR_EGG       0xFFFF   // white
#define COLOR_BOSS_BODY 0xFD20   // boss reads as a giant chicken
#define COLOR_BOSS_ARMOR 0x8410  // grey plating (the "robo" part)
#define COLOR_SHIELD    0x841F   // light blue
#define COLOR_TEXT      0xFFFF
#define COLOR_READY     0xFFFF   // ability icon: ready (bright white)
#define COLOR_COOLDOWN  0x39C7   // ability icon: on cooldown (dim grey-blue)
#define COLOR_HP_GOOD   0x07E0   // green
#define COLOR_HP_MID    0xFFE0   // yellow
#define COLOR_HP_LOW    0xF800   // red

// =====================================================================
// PLAYER SHIP (drawn as hull + wings + cockpit + engine, not a bitmap)
// =====================================================================
#define SHIP_W 18
#define SHIP_H 14
#define PLAYER_Y (SCREEN_H - SHIP_H - 6)
#define PLAYER_SPEED_PX 6          // px per update tick when joystick deflected
#define PLAYER_MAX_HEALTH 100

#define JOY_DEADZONE 80            // +/- around center (center ~512)
#define JOY_CENTER 512

// Weapon tunables (all timing via millis(), never delay())
#define BLASTER_FIRE_INTERVAL_MS 180
#define CLEAVER_COOLDOWN_MS      4000
#define CLEAVER_SPEED_PX         14    // how fast the beam sweeps upward each tick
#define CLEAVER_DAMAGE_PER_FRAME 5     // damage dealt each frame something is inside the beam (nerfed from 8)
#define CLEAVER_BEAM_HEIGHT      10    // thicker bar, easy to notice
#define SHIELD_COOLDOWN_MS       15000
#define SHIELD_DURATION_MS       3000
#define SHIELD_RING_MARGIN       6     // how far the shield bubble extends past the hull

// Bomb: full-screen fire explosion
#define BOMB_CHICKEN_DAMAGE 22    // most regular chickens die, tougher ones survive
#define BOMB_BOSS_DAMAGE    25
#define BOMB_EXPLOSION_MS   1000  // longer so the fire reaches the screen edges
#define BOMB_FREEZE_MS      1100  // slightly longer than animation
#define BOMB_MAX_RADIUS     420   // covers full 320x240 screen from centre

// =====================================================================
// CHICKEN ENEMIES (used for both flying flocks and the stationary elite
// wall -- same sprite/struct, different movement per wave)
// =====================================================================
#define CHICKEN_W 24
#define CHICKEN_H 22
#define EGG_W 6
#define EGG_H 10
#define DRUMSTICK_W_H 8            // 8x8 sprite for the new drumstick shape
#define EGG_MAX_TRAVEL_PX 140   // eggs vanish after falling this far, even mid-air

#define MAX_CHICKENS   10
#define MAX_LASERS     14
#define MAX_EGGS       16
#define MAX_DRUMSTICKS 4

// Regular flock waves (1-4, 6-9)
#define FLOCK_SPAWN_INTERVAL_MS 900   // baseline, scales down with wave
#define FLOCK_MIN_VY 1
#define FLOCK_MAX_VY 3
#define FLOCK_EGG_MIN_INTERVAL_MS 2000
#define FLOCK_EGG_MAX_INTERVAL_MS 5000

// Elite squad wave (5)
#define ELITE_ROWS 2
#define ELITE_COLS 4
#define ELITE_EGG_INTERVAL_MS 1200

// =====================================================================
// BOSS (wave 10) -- drawn as a giant armoured chicken
// =====================================================================
#define BOSS_W 90
#define BOSS_H 64
#define BOSS_MAX_HEALTH 400
#define BOSS_PHASE_DURATION_MS 5000
#define BOSS_VX 2          // horizontal sweep speed -- no longer a stationary target
#define BOSS_MARGIN 14     // extra erase margin to cover wing overhang + health bar

// =====================================================================
// WAVES / TRANSITIONS
// =====================================================================
#define NUM_WAVES 10
#define WAVE_ELITE_SQUAD 5
#define WAVE_BOSS 10

#define WAVE_CLEARED_BANNER_MS 1200   // "WAVE X CLEARED!" hang time
#define WAVE_START_BANNER_MS   900    // "WAVE Y: TYPE" hang time after that

// =====================================================================
// GAME STATES
// =====================================================================
enum GameState : uint8_t {
  STATE_MENU,
  STATE_PLAYING,
  STATE_WAVE_TRANSITION,
  STATE_GAME_OVER,
  STATE_VICTORY
};

enum WaveKind : uint8_t {
  WAVE_FLOCK,       // regular flying chickens
  WAVE_ELITE,       // stationary armoured wall
  WAVE_BOSS_FIGHT
};

#endif
