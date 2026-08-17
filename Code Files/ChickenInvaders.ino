// =====================================================================
// Chicken Invaders: Asteroid & Boss Rush
// Arduino Mega 2560 + LCDWIKI 2.4" MAR2406 (ILI9341, 8-bit parallel)
// =====================================================================
// Libraries required: LCDWIKI_GUI + LCDWIKI_KBV
// =====================================================================

#include <LCDWIKI_GUI.h>
#include <LCDWIKI_KBV.h>
#include "config.h"

LCDWIKI_KBV mylcd(ILI9341, A3, A2, A1, A0, A4); //model,cs,cd,wr,rd,reset

// ---------------------------------------------------------------------
// STRUCTS
// ---------------------------------------------------------------------
struct Player {
  int16_t x, prevX;
  uint8_t health;
  bool shieldActive;
  unsigned long shieldEndTime;
  unsigned long shieldCooldownEnd;
  bool cleaverActive;
  int16_t cleaverBeamY;    // current sweep position, travels upward each frame
  unsigned long cleaverCooldownEnd;
  bool bombUsedThisWave;
  unsigned long lastFireTime;
  uint8_t weaponTier;      // 1-3
  uint8_t weaponProgress;  // 0-100, fills from drumstick pickups
};

struct Laser {
  bool active;
  int16_t x, y, prevY;
  int8_t vy;
  uint8_t damage;
};

// One struct covers BOTH the flying flock chickens (waves 1-4,6-9) and
// the stationary elite wall (wave 5) -- only one kind is ever active at
// a time (per currentWaveKind), so they share the same pool and drawing.
struct Chicken {
  bool active;
  int16_t x, y, prevX, prevY;
  int8_t vx, vy;          // both 0 for elites (stationary)
  int8_t health;
  unsigned long nextEggTime;
};

struct Egg {
  bool active;
  int16_t x, y, prevY, spawnY;
  int8_t vy;
};

struct Drumstick {
  bool active;
  int16_t x, y, prevY;
};

struct Boss {
  int16_t health;
  uint8_t phase;         // 0 = lasers, 1 = spinning shots, 2 = giant eggs
  unsigned long phaseStartTime;
  int16_t x, y, prevX;
  int8_t vx;
};

// ---------------------------------------------------------------------
// GLOBAL STATE
// ---------------------------------------------------------------------
GameState gameState = STATE_MENU;
uint8_t currentWave = 1;
WaveKind currentWaveKind = WAVE_FLOCK;
unsigned long waveStartTime = 0;
unsigned long lastSpawnTime = 0;
uint16_t chickensSpawnedThisWave = 0;
uint16_t chickensTargetThisWave = 12;

bool waveClearShown = false;
bool nextWaveStarted = false;

// Bomb: brief freeze on new spawns/eggs while the explosion animation
// plays, plus state for the non-blocking expanding-ring animation.
unsigned long bombFreezeUntil = 0;
bool bombExplosionActive = false;
unsigned long bombExplosionStartTime = 0;
int16_t bombExplosionX = 0, bombExplosionY = 0;
int16_t bombExplosionPrevRadius = 0;

// Flags to keep HUD healthy after screen clears
bool hudDirty = true;
bool clearScreenNextFrame = false;

Player player;
Laser lasers[MAX_LASERS];
Chicken chickens[MAX_CHICKENS];
Egg eggs[MAX_EGGS];
Drumstick drumsticks[MAX_DRUMSTICKS];
Boss boss;

// ---------------------------------------------------------------------
// LOW-LEVEL DRAW HELPERS
// ---------------------------------------------------------------------
void fillRectWH(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  mylcd.Set_Draw_color(color);
  mylcd.Fill_Rectangle(x, y, x + w - 1, y + h - 1);
}

void fillCircleXY(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  mylcd.Set_Draw_color(color);
  mylcd.Fill_Circle(cx, cy, r);
}

void fillTriXY(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
               int16_t x2, int16_t y2, uint16_t color) {
  mylcd.Set_Draw_color(color);
  mylcd.Fill_Triangle(x0, y0, x1, y1, x2, y2);
}

void eraseSprite(int16_t x, int16_t y, uint8_t w, uint8_t h) {
  fillRectWH(x, y, w, h, COLOR_BG);
}

int16_t activeChickenCount() {
  int16_t n = 0;
  for (uint8_t i = 0; i < MAX_CHICKENS; i++) if (chickens[i].active) n++;
  return n;
}

// Helpers so wave completion can wait for all drops to vanish
int16_t activeEggCount() {
  int16_t n = 0;
  for (uint8_t i = 0; i < MAX_EGGS; i++) if (eggs[i].active) n++;
  return n;
}

int16_t activeDrumstickCount() {
  int16_t n = 0;
  for (uint8_t i = 0; i < MAX_DRUMSTICKS; i++) if (drumsticks[i].active) n++;
  return n;
}

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  pinMode(PIN_BTN_BLASTER, INPUT_PULLUP);
  pinMode(PIN_BTN_CLEAVER, INPUT_PULLUP);
  pinMode(PIN_BTN_SHIELD, INPUT_PULLUP);
  pinMode(PIN_BTN_BOMB, INPUT_PULLUP);

  mylcd.Init_LCD();
  mylcd.Set_Rotation(1); // landscape, 320x240
  mylcd.Set_Text_Mode(0);
  mylcd.Fill_Screen(COLOR_BG);

  randomSeed(analogRead(A15)); // unused floating pin for entropy

  drawMenu();
}

// ---------------------------------------------------------------------
// MAIN LOOP -- strictly non-blocking, no delay()
// ---------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  switch (gameState) {
    case STATE_MENU:
      if (anyButtonPressed()) {
        resetGame();
        startWave(1);
        // Wave 1 now gets the same banner timer as every other wave
        waveStartTime = millis();
        waveClearShown = true;      // skip "CLEARED" phase
        nextWaveStarted = true;     // go straight to banner-wait
        gameState = STATE_WAVE_TRANSITION;
      }
      break;

    case STATE_PLAYING:
      updateGame(now);
      break;

    case STATE_WAVE_TRANSITION: {
      unsigned long elapsed = now - waveStartTime;

      if (!waveClearShown) {
        showWaveCleared(currentWave);
        waveClearShown = true;
      }

      if (!nextWaveStarted && elapsed > WAVE_CLEARED_BANNER_MS) {
        currentWave++;
        if (currentWave > NUM_WAVES) {
          gameState = STATE_VICTORY;
          drawVictory();
        } else {
          startWave(currentWave); // clears screen + shows "WAVE Y: TYPE"
          nextWaveStarted = true;
        }
      }

      // Wait only WAVE_START_BANNER_MS because startWave() reset the timer
      if (nextWaveStarted && elapsed > WAVE_START_BANNER_MS) {
        clearScreenNextFrame = true; // let drawFrame do the wipe on first PLAYING frame
        hudDirty = true;
        gameState = STATE_PLAYING;
        waveClearShown = false;
        nextWaveStarted = false;
      }
      break;
    }

    case STATE_GAME_OVER:
      if (anyButtonPressed()) {
        gameState = STATE_MENU;
        mylcd.Fill_Screen(COLOR_BG);
        drawMenu();
      }
      break;

    case STATE_VICTORY:
      if (anyButtonPressed()) {
        gameState = STATE_MENU;
        mylcd.Fill_Screen(COLOR_BG);
        drawMenu();
      }
      break;
  }
}

bool anyButtonPressed() {
  return !digitalRead(PIN_BTN_BLASTER) || !digitalRead(PIN_BTN_CLEAVER) ||
         !digitalRead(PIN_BTN_SHIELD)  || !digitalRead(PIN_BTN_BOMB);
}

// ---------------------------------------------------------------------
// GAME RESET / WAVE SETUP
// ---------------------------------------------------------------------
void resetGame() {
  player.x = SCREEN_W / 2 - SHIP_W / 2;
  player.prevX = player.x;
  player.health = PLAYER_MAX_HEALTH;
  player.shieldActive = false;
  player.shieldEndTime = 0;
  player.shieldCooldownEnd = 0;
  player.cleaverActive = false;
  player.cleaverBeamY = 0;
  player.cleaverCooldownEnd = 0;
  player.bombUsedThisWave = false;
  player.lastFireTime = 0;
  player.weaponTier = 1;
  player.weaponProgress = 0;

  for (uint8_t i = 0; i < MAX_LASERS; i++) lasers[i].active = false;
  for (uint8_t i = 0; i < MAX_CHICKENS; i++) chickens[i].active = false;
  for (uint8_t i = 0; i < MAX_EGGS; i++) eggs[i].active = false;
  for (uint8_t i = 0; i < MAX_DRUMSTICKS; i++) drumsticks[i].active = false;

  currentWave = 1;
  waveClearShown = false;
  nextWaveStarted = false;
  mylcd.Fill_Screen(COLOR_BG);
  hudDirty = true;
  clearScreenNextFrame = false;
}

void startWave(uint8_t waveNum) {
  waveStartTime = millis();
  lastSpawnTime = waveStartTime;
  player.bombUsedThisWave = false;

  for (uint8_t i = 0; i < MAX_CHICKENS; i++) chickens[i].active = false;
  for (uint8_t i = 0; i < MAX_EGGS; i++) eggs[i].active = false;

  if (waveNum == WAVE_ELITE_SQUAD) {
    currentWaveKind = WAVE_ELITE;
    spawnEliteSquad();
  } else if (waveNum == WAVE_BOSS) {
    currentWaveKind = WAVE_BOSS_FIGHT;
    spawnBoss();
  } else {
    currentWaveKind = WAVE_FLOCK;
    chickensSpawnedThisWave = 0;
    chickensTargetThisWave = 10 + waveNum;
  }

  mylcd.Fill_Screen(COLOR_BG);
  hudDirty = true;
  drawWaveStartBanner(waveNum, currentWaveKind);
}

const char* waveKindName(WaveKind kind) {
  if (kind == WAVE_ELITE) return "ELITE SQUAD";
  if (kind == WAVE_BOSS_FIGHT) return "BOSS FIGHT";
  return "CHICKEN FLOCK";
}

// ---------------------------------------------------------------------
// PER-FRAME UPDATE (PLAYING state)
// ---------------------------------------------------------------------
void updateGame(unsigned long now) {
  readInputs(now);
  updatePlayerMovement();
  updateLasers(now);
  updateDrumsticks(now);
  updateChickens(now);

  if (currentWaveKind == WAVE_BOSS_FIGHT) {
    updateBoss(now);
  }
  updateEggs(now);

  checkCollisions();
  checkWaveCompletion(now);

  drawFrame();

  if (player.health == 0) {
    gameState = STATE_GAME_OVER;
    drawGameOver();
  }
}

// ---------------------------------------------------------------------
// INPUT HANDLING
// ---------------------------------------------------------------------
void readInputs(unsigned long now) {
  if (!digitalRead(PIN_BTN_BLASTER)) {
    uint16_t interval = BLASTER_FIRE_INTERVAL_MS - (player.weaponTier - 1) * 40;
    if (now - player.lastFireTime >= interval) {
      fireBlaster(now);
      player.lastFireTime = now;
    }
  }

  if (!digitalRead(PIN_BTN_CLEAVER) && now >= player.cleaverCooldownEnd && !player.cleaverActive) {
    player.cleaverActive = true;
    player.cleaverBeamY = PLAYER_Y - CLEAVER_BEAM_HEIGHT;
    player.cleaverCooldownEnd = now + CLEAVER_COOLDOWN_MS;
  }
  if (player.cleaverActive) {
    player.cleaverBeamY -= CLEAVER_SPEED_PX;
    if (player.cleaverBeamY < -CLEAVER_BEAM_HEIGHT) {
      player.cleaverActive = false;
    }
  }

  if (!digitalRead(PIN_BTN_SHIELD) && now >= player.shieldCooldownEnd) {
    player.shieldActive = true;
    player.shieldEndTime = now + SHIELD_DURATION_MS;
    player.shieldCooldownEnd = now + SHIELD_COOLDOWN_MS;
  }
  if (player.shieldActive && now >= player.shieldEndTime) {
    player.shieldActive = false;
  }

  if (!digitalRead(PIN_BTN_BOMB) && !player.bombUsedThisWave) {
    triggerBomb(now);
    player.bombUsedThisWave = true;
  }
}

void updatePlayerMovement() {
  int16_t joyVal = analogRead(PIN_JOY_X);
  player.prevX = player.x;
  if (joyVal < JOY_CENTER - JOY_DEADZONE) {
    player.x -= PLAYER_SPEED_PX;
  } else if (joyVal > JOY_CENTER + JOY_DEADZONE) {
    player.x += PLAYER_SPEED_PX;
  }
  if (player.x < 0) player.x = 0;
  if (player.x > SCREEN_W - SHIP_W) player.x = SCREEN_W - SHIP_W;
}

// ---------------------------------------------------------------------
// WEAPONS
// ---------------------------------------------------------------------
void fireBlaster(unsigned long now) {
  for (uint8_t i = 0; i < MAX_LASERS; i++) {
    if (!lasers[i].active) {
      lasers[i].active = true;
      lasers[i].x = player.x + SHIP_W / 2 - 1;
      lasers[i].y = PLAYER_Y - 4;
      lasers[i].prevY = lasers[i].y;
      lasers[i].vy = -6;
      lasers[i].damage = 5 + (player.weaponTier - 1) * 3;
      break;
    }
  }
}

void triggerBomb(unsigned long now) {
  for (uint8_t i = 0; i < MAX_CHICKENS; i++) {
    if (!chickens[i].active) continue;
    chickens[i].health -= BOMB_CHICKEN_DAMAGE;
    if (chickens[i].health <= 0) {
      eraseSprite(chickens[i].prevX, chickens[i].prevY, CHICKEN_W, CHICKEN_H);
      chickens[i].active = false;
      maybeDropDrumstick(chickens[i].x, chickens[i].y);
    }
  }
  for (uint8_t i = 0; i < MAX_EGGS; i++) {
    if (eggs[i].active) {
      eraseSprite(eggs[i].x, eggs[i].prevY, EGG_W, EGG_H);
      eggs[i].active = false;
    }
  }
  if (currentWaveKind == WAVE_BOSS_FIGHT) {
    boss.health -= BOMB_BOSS_DAMAGE;
  }

  // Pause new spawns/eggs while the explosion plays, then resume normally.
  bombFreezeUntil = now + BOMB_FREEZE_MS;
  bombExplosionActive = true;
  bombExplosionStartTime = now;
  bombExplosionX = player.x + SHIP_W / 2;
  bombExplosionY = PLAYER_Y + SHIP_H / 2;
  bombExplosionPrevRadius = 0;
}

// ---------------------------------------------------------------------
// SPAWNING
// ---------------------------------------------------------------------
void spawnFlockIfNeeded(unsigned long now) {
  if (now < bombFreezeUntil) return;
  if (chickensSpawnedThisWave >= chickensTargetThisWave) return;
  uint16_t interval = FLOCK_SPAWN_INTERVAL_MS - (currentWave * 20);
  if (interval < 250) interval = 250;
  if (now - lastSpawnTime < interval) return;

  for (uint8_t i = 0; i < MAX_CHICKENS; i++) {
    if (!chickens[i].active) {
      chickens[i].active = true;
      chickens[i].x = random(4, SCREEN_W - CHICKEN_W - 4);
      chickens[i].y = -CHICKEN_H;
      chickens[i].prevX = chickens[i].x;
      chickens[i].prevY = chickens[i].y;
      chickens[i].vy = random(FLOCK_MIN_VY, FLOCK_MAX_VY + 1);
      chickens[i].vx = random(-1, 2);
      chickens[i].health = 10 + currentWave * 2;
      chickens[i].nextEggTime = now + random(FLOCK_EGG_MIN_INTERVAL_MS, FLOCK_EGG_MAX_INTERVAL_MS);
      chickensSpawnedThisWave++;
      lastSpawnTime = now;
      break;
    }
  }
}

void spawnEliteSquad() {
  uint8_t idx = 0;
  int16_t startX = 20;
  int16_t spacingX = (SCREEN_W - 40) / ELITE_COLS;
  for (uint8_t row = 0; row < ELITE_ROWS && idx < MAX_CHICKENS; row++) {
    for (uint8_t col = 0; col < ELITE_COLS && idx < MAX_CHICKENS; col++) {
      chickens[idx].active = true;
      chickens[idx].x = startX + col * spacingX;
      chickens[idx].y = 10 + row * (CHICKEN_H + 6);
      chickens[idx].prevX = chickens[idx].x;
      chickens[idx].prevY = chickens[idx].y;
      chickens[idx].vx = 0;
      chickens[idx].vy = 0;
      chickens[idx].health = 30;
      chickens[idx].nextEggTime = millis() + random(0, ELITE_EGG_INTERVAL_MS);
      idx++;
    }
  }
}

void spawnBoss() {
  boss.health = BOSS_MAX_HEALTH;
  boss.phase = 0;
  boss.phaseStartTime = millis();
  boss.x = SCREEN_W / 2 - BOSS_W / 2;
  boss.prevX = boss.x;
  boss.y = 10;
  boss.vx = BOSS_VX;
}

void maybeDropDrumstick(int16_t x, int16_t y) {
  if (random(0, 100) < 30) {
    for (uint8_t i = 0; i < MAX_DRUMSTICKS; i++) {
      if (!drumsticks[i].active) {
        drumsticks[i].active = true;
        drumsticks[i].x = x;
        drumsticks[i].y = y;
        drumsticks[i].prevY = y;
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------
// UPDATE: LASERS
// ---------------------------------------------------------------------
void updateLasers(unsigned long now) {
  for (uint8_t i = 0; i < MAX_LASERS; i++) {
    if (!lasers[i].active) continue;
    lasers[i].prevY = lasers[i].y;
    lasers[i].y += lasers[i].vy;
    if (lasers[i].y < -4) {
      eraseSprite(lasers[i].x, lasers[i].prevY, 2, 6);
      lasers[i].active = false;
    }
  }
}

// ---------------------------------------------------------------------
// UPDATE: CHICKENS (flying flock or stationary elite wall)
// ---------------------------------------------------------------------
void updateChickens(unsigned long now) {
  if (currentWaveKind == WAVE_FLOCK) spawnFlockIfNeeded(now);

  for (uint8_t i = 0; i < MAX_CHICKENS; i++) {
    if (!chickens[i].active) continue;
    chickens[i].prevX = chickens[i].x;
    chickens[i].prevY = chickens[i].y;

    if (currentWaveKind == WAVE_FLOCK) {
      chickens[i].y += chickens[i].vy;
      chickens[i].x += chickens[i].vx;

      // bounce off the side walls, and occasionally juke for erratic flight
      if (chickens[i].x <= 2) chickens[i].vx = 1;
      if (chickens[i].x >= SCREEN_W - CHICKEN_W - 2) chickens[i].vx = -1;
      if (random(0, 100) < 2) chickens[i].vx = random(-1, 2);

      if (chickens[i].y > SCREEN_H) {
        eraseSprite(chickens[i].prevX, chickens[i].prevY, CHICKEN_W, CHICKEN_H);
        chickens[i].active = false;
        continue;
      }
    }

    // Egg-dropping applies to both flying flocks and the stationary wall.
    if (now >= bombFreezeUntil && now >= chickens[i].nextEggTime) {
      spawnEgg(chickens[i].x + CHICKEN_W / 2, chickens[i].y + CHICKEN_H);
      unsigned long interval = (currentWaveKind == WAVE_ELITE)
          ? ELITE_EGG_INTERVAL_MS
          : random(FLOCK_EGG_MIN_INTERVAL_MS, FLOCK_EGG_MAX_INTERVAL_MS);
      chickens[i].nextEggTime = now + interval;
    }
  }
}

void spawnEgg(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < MAX_EGGS; i++) {
    if (!eggs[i].active) {
      eggs[i].active = true;
      eggs[i].x = x;
      eggs[i].y = y;
      eggs[i].prevY = y;
      eggs[i].spawnY = y;
      eggs[i].vy = 2 + currentWave / 4;
      break;
    }
  }
}

void updateEggs(unsigned long now) {
  for (uint8_t i = 0; i < MAX_EGGS; i++) {
    if (!eggs[i].active) continue;
    eggs[i].prevY = eggs[i].y;
    eggs[i].y += eggs[i].vy;
    bool offScreen = eggs[i].y > SCREEN_H;
    bool traveledTooFar = (eggs[i].y - eggs[i].spawnY) > EGG_MAX_TRAVEL_PX;
    if (offScreen || traveledTooFar) {
      eraseSprite(eggs[i].x, eggs[i].prevY, EGG_W, EGG_H);
      eggs[i].active = false;
    }
  }
}

void updateDrumsticks(unsigned long now) {
  for (uint8_t i = 0; i < MAX_DRUMSTICKS; i++) {
    if (!drumsticks[i].active) continue;
    drumsticks[i].prevY = drumsticks[i].y;
    drumsticks[i].y += 2;
    if (drumsticks[i].y > SCREEN_H) {
      eraseSprite(drumsticks[i].x, drumsticks[i].prevY, DRUMSTICK_W_H, DRUMSTICK_W_H);
      drumsticks[i].active = false;
    }
  }
}

// ---------------------------------------------------------------------
// UPDATE: BOSS (Wave 10)
// ---------------------------------------------------------------------
void updateBoss(unsigned long now) {
  if (now - boss.phaseStartTime >= BOSS_PHASE_DURATION_MS) {
    boss.phase = (boss.phase + 1) % 3;
    boss.phaseStartTime = now;
  }

  // Boss sweeps side to side instead of sitting still.
  boss.prevX = boss.x;
  boss.x += boss.vx;
  if (boss.x <= 4) boss.vx = BOSS_VX;
  if (boss.x >= SCREEN_W - BOSS_W - 4) boss.vx = -BOSS_VX;

  if (now < bombFreezeUntil) return; // bomb just went off -- give a breather

  switch (boss.phase) {
    case 0: // sweeping lasers -> randomized eggs across the boss's width
      if (random(0, 100) < 10) spawnEgg(boss.x + random(0, BOSS_W), boss.y + BOSS_H);
      break;
    case 1: // spinning projectiles -> a spread of eggs at once, varied positions
      if (random(0, 100) < 14) {
        spawnEgg(boss.x + random(0, BOSS_W / 2), boss.y + BOSS_H);
        spawnEgg(boss.x + BOSS_W / 2 + random(0, BOSS_W / 2), boss.y + BOSS_H);
      }
      break;
    case 2: // giant explosive eggs -> centered but with random jitter, faster fall
      if (random(0, 100) < 5) {
        int16_t gx = boss.x + BOSS_W / 2 + random(-15, 16);
        spawnEgg(gx, boss.y + BOSS_H);
      }
      break;
  }
}

// ---------------------------------------------------------------------
// COLLISIONS (simple AABB)
// ---------------------------------------------------------------------
bool aabb(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
          int16_t bx, int16_t by, int16_t bw, int16_t bh) {
  return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}

void checkCollisions() {
  // Lasers vs chickens (covers both flock and elite -- same struct)
  // NOTE: erases use prevX/prevY, not x/y. By this point in the frame,
  // updateChickens()/updateLasers() have already advanced x/y to the
  // NEXT position, but the screen still shows last frame's draw at
  // prevX/prevY -- erasing at x/y (undrawn) left permanent trace dots.
  for (uint8_t i = 0; i < MAX_LASERS; i++) {
    if (!lasers[i].active) continue;
    for (uint8_t j = 0; j < MAX_CHICKENS; j++) {
      if (!chickens[j].active) continue;
      if (aabb(lasers[i].x, lasers[i].y, 2, 6, chickens[j].x, chickens[j].y,
               CHICKEN_W, CHICKEN_H)) {
        lasers[i].active = false;
        eraseSprite(lasers[i].x, lasers[i].prevY, 2, 6);
        chickens[j].health -= lasers[i].damage;
        if (chickens[j].health <= 0) {
          eraseSprite(chickens[j].prevX, chickens[j].prevY, CHICKEN_W, CHICKEN_H);
          chickens[j].active = false;
          maybeDropDrumstick(chickens[j].x, chickens[j].y);
        }
        break;
      }
    }
  }

  // Lasers vs boss
  if (currentWaveKind == WAVE_BOSS_FIGHT) {
    for (uint8_t i = 0; i < MAX_LASERS; i++) {
      if (!lasers[i].active) continue;
      if (aabb(lasers[i].x, lasers[i].y, 2, 6, boss.x, boss.y, BOSS_W, BOSS_H)) {
        lasers[i].active = false;
        eraseSprite(lasers[i].x, lasers[i].prevY, 2, 6);
        boss.health -= lasers[i].damage;
      }
    }
  }

  // Cleaver: a piercing wave that sweeps from the ship to the top of the
  // screen, damaging anything it passes through along the way.
  if (player.cleaverActive) {
    int16_t beamY = player.cleaverBeamY;
    for (uint8_t j = 0; j < MAX_CHICKENS; j++) {
      if (!chickens[j].active) continue;
      if (chickens[j].y + CHICKEN_H >= beamY && chickens[j].y <= beamY + CLEAVER_BEAM_HEIGHT) {
        chickens[j].health -= CLEAVER_DAMAGE_PER_FRAME;
        if (chickens[j].health <= 0) {
          eraseSprite(chickens[j].prevX, chickens[j].prevY, CHICKEN_W, CHICKEN_H);
          chickens[j].active = false;
          maybeDropDrumstick(chickens[j].x, chickens[j].y);
        }
      }
    }
    if (currentWaveKind == WAVE_BOSS_FIGHT &&
        boss.y + BOSS_H >= beamY && boss.y <= beamY + CLEAVER_BEAM_HEIGHT) {
      boss.health -= CLEAVER_DAMAGE_PER_FRAME / 2;
    }
  }

  // Player vs chickens (contact damage unless shielded -- crashing into
  // one destroys it and hurts you)
  if (!player.shieldActive) {
    for (uint8_t j = 0; j < MAX_CHICKENS; j++) {
      if (!chickens[j].active) continue;
      if (aabb(player.x, PLAYER_Y, SHIP_W, SHIP_H, chickens[j].x, chickens[j].y,
               CHICKEN_W, CHICKEN_H)) {
        eraseSprite(chickens[j].prevX, chickens[j].prevY, CHICKEN_W, CHICKEN_H);
        chickens[j].active = false;
        damagePlayer(12);
      }
    }
    for (uint8_t j = 0; j < MAX_EGGS; j++) {
      if (!eggs[j].active) continue;
      if (aabb(player.x, PLAYER_Y, SHIP_W, SHIP_H, eggs[j].x, eggs[j].y,
               EGG_W, EGG_H)) {
        eraseSprite(eggs[j].x, eggs[j].prevY, EGG_W, EGG_H);
        eggs[j].active = false;
        damagePlayer(10);
      }
    }
  }

  // Player vs drumstick pickups (always collectible, even with shield)
  for (uint8_t j = 0; j < MAX_DRUMSTICKS; j++) {
    if (!drumsticks[j].active) continue;
    if (aabb(player.x, PLAYER_Y, SHIP_W, SHIP_H, drumsticks[j].x, drumsticks[j].y,
             DRUMSTICK_W_H, DRUMSTICK_W_H)) {
      eraseSprite(drumsticks[j].x, drumsticks[j].prevY, DRUMSTICK_W_H, DRUMSTICK_W_H);
      drumsticks[j].active = false;
      collectDrumstick();
    }
  }
}

void damagePlayer(uint8_t amount) {
  if (amount >= player.health) player.health = 0;
  else player.health -= amount;
}

void collectDrumstick() {
  player.weaponProgress += 20;
  if (player.weaponProgress >= 100 && player.weaponTier < 3) {
    player.weaponProgress = 0;
    player.weaponTier++;
  }
  if (player.weaponProgress > 100) player.weaponProgress = 100;
}

// ---------------------------------------------------------------------
// WAVE COMPLETION CHECK
// ---------------------------------------------------------------------
void checkWaveCompletion(unsigned long now) {
  bool waveDone = false;
  // Wave only ends when chickens, eggs, and drumsticks are all gone
  bool noDrops = (activeEggCount() == 0 && activeDrumstickCount() == 0);

  if (currentWaveKind == WAVE_FLOCK) {
    waveDone = (chickensSpawnedThisWave >= chickensTargetThisWave) && (activeChickenCount() == 0) && noDrops;
  } else if (currentWaveKind == WAVE_ELITE) {
    waveDone = (activeChickenCount() == 0) && noDrops;
  } else if (currentWaveKind == WAVE_BOSS_FIGHT) {
    waveDone = (boss.health <= 0) && noDrops;
  }

  if (waveDone) {
    waveStartTime = now;
    waveClearShown = false;
    nextWaveStarted = false;
    gameState = STATE_WAVE_TRANSITION;
  }
}

// ---------------------------------------------------------------------
// RENDERING
// ---------------------------------------------------------------------
void drawFrame() {
  // Deferred full-screen clear (used after bomb or wave transition)
  if (clearScreenNextFrame) {
    mylcd.Fill_Screen(COLOR_BG);
    clearScreenNextFrame = false;
    hudDirty = true;
  }

  if (player.x != player.prevX) {
    eraseSprite(player.prevX, PLAYER_Y, SHIP_W, SHIP_H);
  }
  drawShip(player.x, PLAYER_Y);

  // Bomb explosion: full-screen fire shockwave (red core, orange body, yellow edge)
  if (bombExplosionActive) {
    unsigned long elapsed = millis() - bombExplosionStartTime;
    if (elapsed > BOMB_EXPLOSION_MS) {
      bombExplosionActive = false;
      clearScreenNextFrame = true; // clean wipe after fire fades
      bombExplosionPrevRadius = 0;
    } else {
      int16_t radius = map(elapsed, 0, BOMB_EXPLOSION_MS, 6, BOMB_MAX_RADIUS);
      if (radius != bombExplosionPrevRadius) {
        if (radius > 30) {
          mylcd.Set_Draw_color(0xF800); // red core
          mylcd.Draw_Circle(bombExplosionX, bombExplosionY, radius - 30);
        }
        if (radius > 15) {
          mylcd.Set_Draw_color(0xFD20); // orange body
          mylcd.Draw_Circle(bombExplosionX, bombExplosionY, radius - 15);
        }
        mylcd.Set_Draw_color(0xFFE0); // yellow leading edge
        mylcd.Draw_Circle(bombExplosionX, bombExplosionY, radius);
        bombExplosionPrevRadius = radius;
      }
    }
  }

  // Shield: a bubble ring around the ship, clearly visible while active,
  // and cleanly erased/repositioned as the ship moves or the shield ends.
  static bool shieldWasDrawn = false;
  static int16_t shieldPrevCx = 0, shieldPrevCy = 0;
  int16_t shieldR = SHIP_W / 2 + SHIELD_RING_MARGIN;
  if (player.shieldActive) {
    int16_t cx = player.x + SHIP_W / 2;
    int16_t cy = PLAYER_Y + SHIP_H / 2;
    if (shieldWasDrawn && cx != shieldPrevCx) {
      mylcd.Set_Draw_color(COLOR_BG);
      mylcd.Draw_Circle(shieldPrevCx, shieldPrevCy, shieldR);
    }
    mylcd.Set_Draw_color(COLOR_SHIELD);
    mylcd.Draw_Circle(cx, cy, shieldR);
    shieldWasDrawn = true;
    shieldPrevCx = cx;
    shieldPrevCy = cy;
  } else if (shieldWasDrawn) {
    mylcd.Set_Draw_color(COLOR_BG);
    mylcd.Draw_Circle(shieldPrevCx, shieldPrevCy, shieldR);
    shieldWasDrawn = false;
  }

  for (uint8_t i = 0; i < MAX_LASERS; i++) {
    if (!lasers[i].active) continue;
    if (lasers[i].y != lasers[i].prevY) eraseSprite(lasers[i].x, lasers[i].prevY, 2, 6);
    fillRectWH(lasers[i].x, lasers[i].y, 2, 6, COLOR_LASER);
  }

  bool armored = (currentWaveKind == WAVE_ELITE);
  for (uint8_t i = 0; i < MAX_CHICKENS; i++) {
    if (!chickens[i].active) continue;
    if (chickens[i].x != chickens[i].prevX || chickens[i].y != chickens[i].prevY) {
      eraseSprite(chickens[i].prevX, chickens[i].prevY, CHICKEN_W, CHICKEN_H);
    }
    drawChicken(chickens[i].x, chickens[i].y, armored);
  }

  if (currentWaveKind == WAVE_BOSS_FIGHT) {
    if (boss.x != boss.prevX) {
      eraseSprite(boss.prevX - BOSS_MARGIN, boss.y - 10, BOSS_W + BOSS_MARGIN * 2, BOSS_H + 18);
    }
    drawBoss();
  }

  for (uint8_t i = 0; i < MAX_EGGS; i++) {
    if (!eggs[i].active) continue;
    if (eggs[i].y != eggs[i].prevY) eraseSprite(eggs[i].x, eggs[i].prevY, EGG_W, EGG_H);
    drawEgg(eggs[i].x, eggs[i].y);
  }

  // Draw actual drumsticks instead of orange blocks
  for (uint8_t i = 0; i < MAX_DRUMSTICKS; i++) {
    if (!drumsticks[i].active) continue;
    if (drumsticks[i].y != drumsticks[i].prevY) {
      eraseSprite(drumsticks[i].x, drumsticks[i].prevY, DRUMSTICK_W_H, DRUMSTICK_W_H);
    }
    drawDrumstick(drumsticks[i].x, drumsticks[i].y);
  }

  // Cleaver beam -- sweeps from the ship to the top of the screen,
  // erasing its previous row every frame it moves, and clearing the
  // final row the moment it finishes.
  static bool cleaverWasDrawn = false;
  static int16_t cleaverPrevDrawY = 0;
  if (player.cleaverActive) {
    if (cleaverWasDrawn && cleaverPrevDrawY != player.cleaverBeamY) {
      fillRectWH(0, cleaverPrevDrawY, SCREEN_W, CLEAVER_BEAM_HEIGHT, COLOR_BG);
    }
    fillRectWH(0, player.cleaverBeamY, SCREEN_W, CLEAVER_BEAM_HEIGHT, COLOR_CLEAVER);
    cleaverWasDrawn = true;
    cleaverPrevDrawY = player.cleaverBeamY;
  } else if (cleaverWasDrawn) {
    fillRectWH(0, cleaverPrevDrawY, SCREEN_W, CLEAVER_BEAM_HEIGHT, COLOR_BG);
    cleaverWasDrawn = false;
  }

  drawHUD();
}

// ---------------------------------------------------------------------
// SHIP: hull + wings + cockpit + a flickering engine, instead of a
// plain triangle.
// ---------------------------------------------------------------------
void drawShip(int16_t x, int16_t y) {
  uint16_t hullColor = player.shieldActive ? COLOR_SHIELD : COLOR_PLAYER;

  // Hull: nose at top-center, tapering down to the body.
  fillTriXY(x + SHIP_W / 2, y, x + 5, y + SHIP_H - 4, x + SHIP_W - 5, y + SHIP_H - 4, hullColor);
  fillRectWH(x + 5, y + SHIP_H - 6, SHIP_W - 10, 6, hullColor);

  // Wings: swept-back triangles on each side.
  fillTriXY(x, y + SHIP_H - 2, x + 6, y + SHIP_H - 8, x + 6, y + SHIP_H - 2, COLOR_WING);
  fillTriXY(x + SHIP_W, y + SHIP_H - 2, x + SHIP_W - 6, y + SHIP_H - 8, x + SHIP_W - 6, y + SHIP_H - 2, COLOR_WING);

  // Cockpit canopy.
  fillCircleXY(x + SHIP_W / 2, y + 5, 2, COLOR_COCKPIT);

  // Engine flicker (redrawn every frame regardless of movement).
  uint16_t engineColor = (millis() / 100 % 2 == 0) ? COLOR_ENGINE_A : COLOR_ENGINE_B;
  fillRectWH(x + SHIP_W / 2 - 2, y + SHIP_H - 2, 4, 3, engineColor);
}

// ---------------------------------------------------------------------
// EGG: small falling body with a short motion trail above it. Both
// parts stay strictly inside the EGG_W x EGG_H box -- the previous
// version drew a radius-2 circle (which is actually 5px wide, not 4)
// inside a 4x4 erase box, so every single egg left a 1px sliver behind
// as it fell. This box is sized with margin so that can't happen.
// ---------------------------------------------------------------------
void drawEgg(int16_t x, int16_t y) {
  fillRectWH(x + 2, y, 2, 4, 0x9CD3);           // faint trail, above the egg
  fillCircleXY(x + 3, y + 7, 2, COLOR_EGG);     // egg body, safely inside the box
}

// ---------------------------------------------------------------------
// CHICKEN: white body, red comb, angry eyes, orange beak/feet, spread
// wings -- with an optional blue armored vest for the elite wall (wave
// 5), matching the reference art. Every shape here stays strictly
// inside [x, x+CHICKEN_W) x [y, y+CHICKEN_H) -- previously the wing
// tips and feet poked 1-2px past that box, which is smaller than the
// erase rectangle used elsewhere, so those edge pixels never got
// cleared and streaked into long trailing lines as chickens moved.
// ---------------------------------------------------------------------
void drawChicken(int16_t x, int16_t y, bool armored) {
  const uint16_t white = 0xFFFF;
  const uint16_t wing  = 0xDEFB; // slightly shaded white for wings
  const uint16_t beak  = 0xFD20; // orange

  // Wings, spread to the sides but kept inside the box.
  fillTriXY(x, y + 9, x + 6, y + 3, x + 6, y + CHICKEN_H - 5, wing);
  fillTriXY(x + CHICKEN_W - 1, y + 9, x + CHICKEN_W - 7, y + 3, x + CHICKEN_W - 7, y + CHICKEN_H - 5, wing);

  // Body: rounded torso.
  fillCircleXY(x + CHICKEN_W / 2, y + CHICKEN_H / 2 + 1, CHICKEN_W / 2 - 5, white);

  // Head, above the body.
  fillCircleXY(x + CHICKEN_W / 2, y + 6, 6, white);

  // Comb on top of the head.
  fillTriXY(x + CHICKEN_W / 2 - 4, y + 1, x + CHICKEN_W / 2 + 4, y + 1, x + CHICKEN_W / 2, y + 6, COLOR_COMB);

  // Angry eyebrows -- small dark wedges angled down toward the beak.
  fillTriXY(x + CHICKEN_W / 2 - 7, y + 2, x + CHICKEN_W / 2 - 1, y + 4, x + CHICKEN_W / 2 - 7, y + 5, 0x0000);
  fillTriXY(x + CHICKEN_W / 2 + 7, y + 2, x + CHICKEN_W / 2 + 1, y + 4, x + CHICKEN_W / 2 + 7, y + 5, 0x0000);

  // Beak.
  fillTriXY(x + CHICKEN_W / 2 - 3, y + 7, x + CHICKEN_W / 2 + 3, y + 7, x + CHICKEN_W / 2, y + 11, beak);

  // Armored vest (elite squad only) -- blue quilted chest like the ref image.
  if (armored) {
    fillRectWH(x + CHICKEN_W / 2 - 7, y + CHICKEN_H / 2 + 1, 14, CHICKEN_H / 2 - 6, COLOR_VEST);
    fillRectWH(x + CHICKEN_W / 2 - 7, y + CHICKEN_H - 6, 14, 2, COLOR_VEST_DK);
  }

  // Feet -- kept a row above the very bottom edge so nothing overhangs.
  fillRectWH(x + CHICKEN_W / 2 - 5, y + CHICKEN_H - 3, 2, 2, beak);
  fillRectWH(x + CHICKEN_W / 2 + 3, y + CHICKEN_H - 3, 2, 2, beak);
}

// ---------------------------------------------------------------------
// BOSS: same chicken design, scaled way up, always armored, with a
// glowing eye that signals the current attack phase plus a health bar.
// ---------------------------------------------------------------------
void drawBoss() {
  const uint16_t white = 0xFFFF;
  const uint16_t wing  = 0xDEFB;
  const uint16_t beak  = 0xFD20;

  fillTriXY(boss.x - 10, boss.y + 30, boss.x + 15, boss.y + 8, boss.x + 15, boss.y + BOSS_H - 10, wing);
  fillTriXY(boss.x + BOSS_W + 10, boss.y + 30, boss.x + BOSS_W - 15, boss.y + 8, boss.x + BOSS_W - 15, boss.y + BOSS_H - 10, wing);

  fillCircleXY(boss.x + BOSS_W / 2, boss.y + BOSS_H / 2 + 8, BOSS_W / 2 - 10, white);
  fillCircleXY(boss.x + BOSS_W / 2, boss.y + 16, 16, white);

  fillTriXY(boss.x + BOSS_W / 2 - 10, boss.y, boss.x + BOSS_W / 2 + 10, boss.y, boss.x + BOSS_W / 2, boss.y + 14, COLOR_COMB);

  // Glowing eye -- color signals attack phase.
  uint16_t eyeColor = (boss.phase == 0) ? COLOR_LASER : (boss.phase == 1) ? COLOR_CLEAVER : COLOR_EGG;
  fillCircleXY(boss.x + BOSS_W / 2, boss.y + 18, 6, eyeColor);

  // Beak.
  fillTriXY(boss.x + BOSS_W / 2 - 6, boss.y + 22, boss.x + BOSS_W / 2 + 6, boss.y + 22, boss.x + BOSS_W / 2, boss.y + 30, beak);

  // Armored vest across the chest.
  fillRectWH(boss.x + BOSS_W / 2 - 20, boss.y + BOSS_H / 2 + 4, 40, BOSS_H / 2 - 8, COLOR_VEST);
  fillRectWH(boss.x + BOSS_W / 2 - 20, boss.y + BOSS_H - 10, 40, 4, COLOR_VEST_DK);

  int16_t barW = map(boss.health, 0, BOSS_MAX_HEALTH, 0, BOSS_W);
  if (barW < 0) barW = 0;
  fillRectWH(boss.x, boss.y - 8, BOSS_W, 4, COLOR_BG);
  fillRectWH(boss.x, boss.y - 8, barW, 4, COLOR_HP_GOOD);
}

// ---------------------------------------------------------------------
// DRUMSTICK: tiny 8x8 meat+bone so it actually looks like a drumstick
// Brown meatball + ivory bone, classic chicken-drumstick look
// ---------------------------------------------------------------------
void drawDrumstick(int16_t x, int16_t y) {
  // Meat (cooked brown/orange-brown)
  fillCircleXY(x + 3, y + 3, 3, 0x0274);   // brown meatball (R/B pre-swapped for this panel)
  // Bone (ivory/off-white stick)
  fillRectWH(x + 2, y + 5, 4, 3, 0xFFDF);  // ivory bone sticking out
}

// ---------------------------------------------------------------------
// HUD: health BAR (not just a number), wave, weapon tier, and small
// ability-status icons so cooldowns are actually visible.
// ---------------------------------------------------------------------
void drawHUD() {
  static uint8_t lastHealth = 255;
  static uint8_t lastTier = 0;
  static uint8_t lastWave = 0;
  static uint8_t lastProgress = 255;

  mylcd.Set_Text_Back_colour(COLOR_BG);
  mylcd.Set_Text_Size(1);

  // Health bar is drawn every frame so it can never vanish
  int16_t barW = map(player.health, 0, PLAYER_MAX_HEALTH, 0, 60);
  uint16_t hpColor = (player.health > 60) ? COLOR_HP_GOOD : (player.health > 25) ? COLOR_HP_MID : COLOR_HP_LOW;
  fillRectWH(20, 1, 60, 8, 0x2104);   // bar background/frame
  fillRectWH(20, 1, barW, 8, hpColor);

  // Redraw text labels every frame so they never disappear
  fillRectWH(0, 0, 20, 16, COLOR_BG);
  mylcd.Set_Text_colour(COLOR_TEXT);
  mylcd.Print_String("HP", 0, 0);
  lastHealth = player.health;

  fillRectWH(SCREEN_W - 60, 0, 60, 10, COLOR_BG);
  mylcd.Set_Text_colour(COLOR_TEXT);
  mylcd.Print_String("TIER:", SCREEN_W - 60, 0);
  mylcd.Print_Number_Int(player.weaponTier, SCREEN_W - 12, 0, 0, ' ', 10);
  lastTier = player.weaponTier;

  fillRectWH(SCREEN_W / 2 - 30, 0, 60, 10, COLOR_BG);
  mylcd.Set_Text_colour(COLOR_TEXT);
  mylcd.Print_String("WAVE", SCREEN_W / 2 - 30, 0);
  mylcd.Print_Number_Int(currentWave, SCREEN_W / 2 + 6, 0, 0, ' ', 10);
  lastWave = currentWave;

  // Weapon upgrade progress bar under TIER text
  int16_t progBarW = map(player.weaponProgress, 0, 100, 0, 50);
  uint16_t progColor = 0xFFE0; // yellow progress
  fillRectWH(SCREEN_W - 55, 11, 50, 4, 0x2104);   // dark background
  fillRectWH(SCREEN_W - 55, 11, progBarW, 4, progColor);
  lastProgress = player.weaponProgress;

  if (hudDirty) hudDirty = false;

  drawAbilityIcons();
}

// Three small squares bottom-right: bright = ready, dim = on cooldown.
// Cheap enough to redraw every frame (3 tiny rects).
void drawAbilityIcons() {
  unsigned long now = millis();
  int16_t iconY = SCREEN_H - 10;
  fillRectWH(SCREEN_W - 34, iconY, 8, 8, (now >= player.cleaverCooldownEnd) ? COLOR_CLEAVER : COLOR_COOLDOWN);
  fillRectWH(SCREEN_W - 22, iconY, 8, 8, (now >= player.shieldCooldownEnd) ? COLOR_SHIELD : COLOR_COOLDOWN);
  fillRectWH(SCREEN_W - 10, iconY, 8, 8, (!player.bombUsedThisWave) ? COLOR_READY : COLOR_COOLDOWN);
}

// ---------------------------------------------------------------------
// WAVE TRANSITION BANNERS -- makes it obvious a wave ended and a new
// one is starting, instead of the game abruptly clearing and continuing.
// ---------------------------------------------------------------------
void showWaveCleared(uint8_t wave) {
  mylcd.Fill_Screen(COLOR_BG);
  hudDirty = true;
  mylcd.Set_Text_Back_colour(COLOR_BG);
  mylcd.Set_Text_colour(COLOR_HP_GOOD);
  mylcd.Set_Text_Size(2);
  mylcd.Print_String("WAVE", 100, 100);
  mylcd.Print_Number_Int(wave, 170, 100, 0, ' ', 10);
  mylcd.Set_Text_Size(1);
  mylcd.Print_String("CLEARED!", 100, 130);
}

void drawWaveStartBanner(uint8_t wave, WaveKind kind) {
  mylcd.Set_Text_Back_colour(COLOR_BG);
  mylcd.Set_Text_colour(COLOR_TEXT);
  mylcd.Set_Text_Size(2);
  mylcd.Print_String("WAVE", 100, 100);
  mylcd.Print_Number_Int(wave, 170, 100, 0, ' ', 10);
  mylcd.Set_Text_Size(1);
  mylcd.Print_String(waveKindName(kind), 90, 130);
}

void drawMenu() {
  mylcd.Fill_Screen(COLOR_BG);
  hudDirty = true;
  mylcd.Set_Text_Back_colour(COLOR_BG);
  mylcd.Set_Text_colour(COLOR_TEXT);
  mylcd.Set_Text_Size(2);
  mylcd.Print_String("CHICKEN INVADERS", 40, 90);
  mylcd.Set_Text_Size(1);
  mylcd.Print_String("Press any button to start", 70, 120);
}

void drawGameOver() {
  mylcd.Fill_Screen(COLOR_BG);
  hudDirty = true;
  mylcd.Set_Text_Back_colour(COLOR_BG);
  mylcd.Set_Text_colour(0xF800);
  mylcd.Set_Text_Size(2);
  mylcd.Print_String("GAME OVER", 90, 100);
  mylcd.Set_Text_colour(COLOR_TEXT);
  mylcd.Set_Text_Size(1);
  mylcd.Print_String("Press any button for menu", 70, 130);
}

void drawVictory() {
  mylcd.Fill_Screen(COLOR_BG);
  hudDirty = true;
  mylcd.Set_Text_Back_colour(COLOR_BG);
  mylcd.Set_Text_colour(0x07E0);
  mylcd.Set_Text_Size(2);
  mylcd.Print_String("VICTORY!", 70, 100);
  mylcd.Set_Text_colour(COLOR_TEXT);
  mylcd.Set_Text_Size(1);
  mylcd.Print_String("Press any button for menu", 70, 130);
}
