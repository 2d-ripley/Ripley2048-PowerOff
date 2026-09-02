#include <Arduino.h>
#include <SD.h>
#include <M5Unified.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include "esp32-hal-bt.h"
#include <vector>
#include <algorithm>

// ============================================================
// RIPLEY2048 PowerOff Edition v1.3.1 — STANDALONE GAME + ALBUM + 2-MIN AUTO-OFF
// ============================================================
// Native PaperS3 power behavior:
//   single click side button = power ON
//   double click side button = hardware main-power OFF
//
// IMPORTANT:
// We do NOT use touch-wake light sleep in this edition.
// While powered on the app stays awake and responsive.
// On hardware power-off, E-Ink retains the last visible image and all
// ESP32/touch activity stops with the PaperS3 main power.
//
// To make a cold power-on feel like "resume", the active game is
// checkpointed to NVS after every valid move/new-game/load, and the
// last user mode (GAME or ALBUM) is persisted. Album already persists
// the last displayed photo.
//
// RTC/Clock/Alarm/Speaker: disabled.
// Wi-Fi/Bluetooth: explicitly OFF.
// Album rendering: proven v4 HQ path unchanged.
// ============================================================

// ============================================================
// M5PaperS3 SD
// ============================================================

#define SD_CS   47
#define SD_SCK  39
#define SD_MOSI 38
#define SD_MISO 40

static constexpr int W = 540;
static constexpr int H = 960;

// ============================================================
// USER UI SETTINGS — RIPLEY EDITION
// ============================================================

// ------------------------------------------------------------
// BOARD
// ------------------------------------------------------------
// 4 x 106 + 3 x 6 = 442 px, centered on the 540 px display.
static constexpr int BOARD_X = 78;
static constexpr int BOARD_Y = 535;
static constexpr int TILE_SIZE = 92;
static constexpr int TILE_GAP = 5;

// Tile grayscale. Empty tiles are deliberately LIGHT for the
// new clean E-Ink UI (no more black empty squares).
static constexpr uint8_t EMPTY_TILE_GRAY = 236;
static constexpr uint8_t TILE_2_GRAY      = 250;
static constexpr uint8_t TILE_4_GRAY      = 225;
static constexpr uint8_t TILE_8_GRAY      = 205;
static constexpr uint8_t TILE_16_GRAY     = 185;
static constexpr uint8_t TILE_32_GRAY     = 165;
static constexpr uint8_t TILE_64_GRAY     = 145;
static constexpr uint8_t TILE_128_GRAY    = 225;
static constexpr uint8_t TILE_256_GRAY    = 205;
static constexpr uint8_t TILE_512_GRAY    = 185;
static constexpr uint8_t TILE_1024_GRAY   = 165;
static constexpr uint8_t TILE_2048_GRAY   = 145;
static constexpr uint8_t TILE_4096_GRAY   = 225;
static constexpr uint8_t TILE_8192_GRAY   = 205;
static constexpr uint8_t TILE_16384_GRAY  = 185;
static constexpr uint8_t TILE_32768_GRAY  = 165;
static constexpr uint8_t TILE_65536_GRAY  = 145;
static constexpr uint8_t TILE_131072_GRAY = 225;

static constexpr uint8_t UI_BLACK_GRAY = 0;
static constexpr uint8_t UI_DARK_GRAY  = 85;
static constexpr uint8_t UI_MID_GRAY   = 170;
static constexpr uint8_t UI_LIGHT_GRAY = 225;
static constexpr uint8_t UI_WHITE_GRAY = 255;

// ------------------------------------------------------------
// RIPLEY BACKGROUND ART
// ------------------------------------------------------------
// New artwork is a wide, integrated illustration rather than a
// small framed picture. Keep the artwork itself mostly white with
// only 3–4 gray levels and lots of negative space.
//
// Eight 500 x 400 RGB565 growth images are embedded in firmware flash.
// microSD is NOT used by the 2048 game artwork anymore.
static constexpr int BACKGROUND_X = 20;
static constexpr int BACKGROUND_Y = 75;
static constexpr int BACKGROUND_W = 500;
static constexpr int BACKGROUND_H = 400;

// PaperS3/M5GFX can occasionally leave a 1-pixel artifact at
// the left edge of an RGB565 image. Mask a tiny strip afterwards.
static constexpr int BACKGROUND_LEFT_EDGE_MASK = 2;

extern "C" {
  extern const uint8_t ripley_bg0_start[] asm("ripley_bg0_start");
  extern const uint8_t ripley_bg0_end[]   asm("ripley_bg0_end");
  extern const uint8_t ripley_bg1_start[] asm("ripley_bg1_start");
  extern const uint8_t ripley_bg1_end[]   asm("ripley_bg1_end");
  extern const uint8_t ripley_bg2_start[] asm("ripley_bg2_start");
  extern const uint8_t ripley_bg2_end[]   asm("ripley_bg2_end");
  extern const uint8_t ripley_bg3_start[] asm("ripley_bg3_start");
  extern const uint8_t ripley_bg3_end[]   asm("ripley_bg3_end");
  extern const uint8_t ripley_bg4_start[] asm("ripley_bg4_start");
  extern const uint8_t ripley_bg4_end[]   asm("ripley_bg4_end");
  extern const uint8_t ripley_bg5_start[] asm("ripley_bg5_start");
  extern const uint8_t ripley_bg5_end[]   asm("ripley_bg5_end");
  extern const uint8_t ripley_bg6_start[] asm("ripley_bg6_start");
  extern const uint8_t ripley_bg6_end[]   asm("ripley_bg6_end");
  extern const uint8_t ripley_bg7_start[] asm("ripley_bg7_start");
  extern const uint8_t ripley_bg7_end[]   asm("ripley_bg7_end");
}

static const uint8_t *const BACKGROUND_DATA[8] = {
  ripley_bg0_start, ripley_bg1_start, ripley_bg2_start, ripley_bg3_start,
  ripley_bg4_start, ripley_bg5_start, ripley_bg6_start, ripley_bg7_start
};

static const uint8_t *const BACKGROUND_END[8] = {
  ripley_bg0_end, ripley_bg1_end, ripley_bg2_end, ripley_bg3_end,
  ripley_bg4_end, ripley_bg5_end, ripley_bg6_end, ripley_bg7_end
};

// Dedicated full alarm artwork (500 x 400 RGB565).
static const char *ALARM_IMAGE_FILE = "/2048ripleybg/alarm.bin";


// Tile number
static constexpr int TILE_FONT_MAX = 4;
static constexpr int TILE_TEXT_MARGIN = 10;

// Rounded UI
static constexpr int TILE_RADIUS = 10;
static constexpr int BUTTON_RADIUS = 10;
static constexpr int CARD_RADIUS = 10;

// ------------------------------------------------------------
// TITLE
// ------------------------------------------------------------
static constexpr int TITLE_Y = 8;
static constexpr int TITLE_SIZE = 7;

// Generous touch hit-box around centered title.
static constexpr int TITLE_TOUCH_X = 145;
static constexpr int TITLE_TOUCH_Y = 0;
static constexpr int TITLE_TOUCH_W = 250;
static constexpr int TITLE_TOUCH_H = 72;

// ------------------------------------------------------------
// SCORE / BEST CARDS
// ------------------------------------------------------------
static constexpr int SCORE_CARD_X = 78;
static constexpr int BEST_CARD_X = 311;
static constexpr int SCORE_CARD_Y = 489;
static constexpr int SCORE_CARD_W = 150;
static constexpr int SCORE_CARD_H = 40;
static constexpr int SCORE_LABEL_SIZE = 1;
static constexpr int SCORE_VALUE_SIZE = 2;

// Legacy aliases used by a few redraw paths.
static constexpr int SCORE_X = SCORE_CARD_X;
static constexpr int SCORE_Y = SCORE_CARD_Y;
static constexpr int SCORE_SIZE = SCORE_VALUE_SIZE;
static constexpr int BEST_X = BEST_CARD_X;
static constexpr int BEST_Y = SCORE_CARD_Y;
static constexpr int BEST_SIZE = SCORE_VALUE_SIZE;

// ------------------------------------------------------------
// SIDE SAVE / LOAD CONTROLS
// ------------------------------------------------------------
// Left side:  outline DOWN TRIANGLE = SAVE, then slots 1 / 2 / 3.
// Right side: outline UP TRIANGLE = LOAD, then slots 1 / 2 / 3.
// NEW GAME is the counter-clockwise circular-arrow icon below LOAD slot 3.
//
// The 500 x 400 Ripley artwork remains full width behind this
// area. Keep important artwork away from the extreme left/right
// edges so the controls stay visually clear.
static constexpr int SIDE_ICON_SIZE = 50;

static constexpr int SAVE_ICON_X = 10;
static constexpr int LOAD_ICON_X = 500;
static constexpr int SIDE_ICON_Y = 128;

static constexpr int SAVE_SLOT_X = 3;
static constexpr int LOAD_SLOT_X = 483;

static constexpr int SLOT_Y1 = 172;
static constexpr int SLOT_Y2 = 242;
static constexpr int SLOT_Y3 = 312;

static constexpr int SLOT_W = 54;
static constexpr int SLOT_H = 50;
static constexpr int SLOT_FONT_SIZE = 3;
static constexpr int SELECTED_BORDER_INSET = 3;

// NEW GAME: icon only, below LOAD slot 3.
static constexpr int NEW_X = 481;
static constexpr int NEW_Y = 384;
static constexpr int NEW_W = 56;
static constexpr int NEW_H = 56;

// ------------------------------------------------------------
// GAME OVER
// ------------------------------------------------------------
static constexpr int GAMEOVER_X = 80;
static constexpr int GAMEOVER_Y = 635;
static constexpr int GAMEOVER_W = 380;
static constexpr int GAMEOVER_H = 120;
static constexpr int GAMEOVER_TITLE_SIZE = 3;
static constexpr int GAMEOVER_TITLE_Y = 657;
static constexpr int GAMEOVER_HINT_SIZE = 2;
static constexpr int GAMEOVER_HINT_Y = 710;

// ------------------------------------------------------------
// STATUS BAR / CLOCK / LONG PRESS
// ------------------------------------------------------------
// Original hidden clock / RTC logic is preserved.
static constexpr int STATUS_BAR_Y = 934;
static constexpr int STATUS_BAR_H = 26;
static constexpr int STATUS_FONT_SIZE = 2;

static constexpr uint32_t TITLE_LONG_PRESS_MS = 1000;
static constexpr uint32_t CLOCK_LONG_PRESS_MS = 1000;
static constexpr uint32_t BG_REFRESH_LONG_PRESS_MS = 1000;

// ------------------------------------------------------------
// LOW-RISK POWER SAVING
// ------------------------------------------------------------
// Keep GAME touch response essentially unchanged, but stop hammering
// the hardware RTC hundreds of times per second while idle.
//
// Alarm timing only needs minute-level accuracy, so checking the RTC
// twice per second is already far more than enough.  Clock redraw
// polling is once per second; the screen itself still redraws only
// when the displayed minute changes.
static constexpr uint32_t ALARM_CHECK_INTERVAL_MS = 500;
static constexpr uint32_t CLOCK_CHECK_INTERVAL_MS = 1000;

// Idle loop delays by mode.  CLOCK mode is intentionally slower
// because E-Ink does not need continuous refresh.  40 ms still gives
// ~25 touch scans/second, which is responsive for taps/long-presses.
// SETTINGS deliberately stay at the mature build's original 5 ms
// because M5.Touch wasReleased() events must not be over-throttled.
static constexpr uint32_t GAME_IDLE_DELAY_MS = 5;
// Conservative CLOCK power saving.
static constexpr uint32_t CLOCK_IDLE_DELAY_MS = 200;
static constexpr uint32_t SETTINGS_IDLE_DELAY_MS = 15;
static constexpr uint32_t ALARM_IDLE_DELAY_MS = 15;

// ============================================================
// POWER MANAGEMENT v4 — XREADER-PRECISION DISPLAY SLEEP
// ============================================================
// The old v2.2 path woke the ESP32 every 50–100 ms.  That is still
// thousands of wakeups while an E-Ink screen is visually static.
//
// v3 follows the architecture used by dedicated e-reader firmware:
//   * stay fully awake while drawing / touching
//   * once the panel is settled, enter ONE long light-sleep
//   * wake immediately from the PaperS3 GT911 INT (GPIO48)
//   * otherwise wake at the next minute boundary for Clock / Alarm
//
// M5Unified knows the PaperS3 wake pin and uses GPIO light-sleep
// wake for GPIO48, which is not an RTC IO.  This is intentionally
// done through M5.Power.lightSleep() instead of raw esp_light_sleep_start().
static constexpr uint32_t POWER_SLEEP_GRACE_MS = 3000;
static constexpr uint64_t IDLE_FALLBACK_SLEEP_US = 30000000ULL; // 30 s
static constexpr uint64_t MIN_SLEEP_US = 250000ULL;              // 0.25 s
static uint32_t powerAwakeUntilMs = 0;

// ============================================================
// PHOTO ALBUM
// ============================================================
// SD folder: /album
// Album auto-orients per photo: landscape 960x540, portrait 540x960.
// No slideshow/timer: the E-Ink remains static until the user interacts.
static constexpr const char *ALBUM_FOLDER = "/album";
static constexpr uint32_t PHOTO_LONG_PRESS_MS = 1000;
static constexpr uint32_t PHOTO_DOUBLE_TAP_MS = 420;
static constexpr int PHOTO_SWIPE_THRESHOLD = 55;
static constexpr int PHOTO_TAP_MOVE = 30;
static constexpr int PHOTO_LIST_ROWS = 7;
static constexpr int PHOTO_LIST_ROW_H = 58;
static constexpr int PHOTO_LIST_TOP = 82;
static constexpr uint32_t PHOTO_IDLE_DELAY_MS = 40;
static constexpr int PHOTO_CORNER_HOTZONE = 120;
static constexpr uint32_t PHOTO_CORNER_REFRESH_MS = 1000;

// Clock mode — landscape 960 x 540.
static constexpr int CLOCK_W = 960;
static constexpr int CLOCK_H = 540;
// Landscape desk-clock layout.
// Time gets almost the full width, but auto-fit still checks both
// width and height so even "88:88" cannot be clipped.
static constexpr int CLOCK_FONT_MAX = 22;
static constexpr int CLOCK_TIME_SIDE_MARGIN = 42;
static constexpr int CLOCK_TIME_TOP = 22;
static constexpr int CLOCK_TIME_H = 285;

static constexpr int CLOCK_DATE_Y = 305;
static constexpr int CLOCK_DATE_SIZE = 4;

static constexpr int CLOCK_STATUS_Y = 374;
static constexpr int CLOCK_STATUS_SIDE_MARGIN = 45;
static constexpr int CLOCK_BATTERY_SIZE = 3;
static constexpr int CLOCK_ALARM_STATUS_SIZE = 3;

static constexpr int CLOCK_SET_X = 220;
static constexpr int CLOCK_SET_Y = 450;
static constexpr int CLOCK_SET_W = 230;
static constexpr int CLOCK_SET_H = 62;

static constexpr int CLOCK_ALARM_X = 510;
static constexpr int CLOCK_ALARM_Y = 450;
static constexpr int CLOCK_ALARM_W = 230;
static constexpr int CLOCK_ALARM_H = 62;

// Alarm setting screen.
static constexpr int ALARM_SET_TITLE_Y = 105;
static constexpr int ALARM_SET_VALUE_Y = 245;
static constexpr int ALARM_SET_VALUE_SIZE = 5;
static constexpr int ALARM_RING_TITLE_Y = 180;
static constexpr int ALARM_RING_TIME_Y = 360;

static constexpr int RTC_SET_TITLE_Y = 55;
static constexpr int RTC_SET_VALUE_SIZE = 4;
static constexpr int RTC_SET_BUTTON_W = 70;
static constexpr int RTC_SET_BUTTON_H = 55;

// Safety anti-ghosting refresh.
// 500 = full refresh every 500 successful moves.
// 0   = disable automatic move-count full refresh entirely.

// ============================================================
// GAME STATE
// ============================================================

uint64_t board[4][4] = {};
uint64_t oldBoard[4][4] = {};

uint64_t score = 0;
uint64_t oldScore = 0;

uint64_t bestScore = 0;
uint64_t oldBestScore = 0;

bool gameOver = false;
bool oldGameOver = false;

int backgroundLevel = 0;
int oldBackgroundLevel = 0;

// ============================================================
// E-INK
// ============================================================

static constexpr int FULL_REFRESH_INTERVAL = 500;

int moveCount = 0;
bool forceFullRefresh = true;

// ============================================================
// TOUCH
// ============================================================

int touchStartX = 0;
int touchStartY = 0;
bool touchTracking = false;
uint32_t touchStartMs = 0;

// Hidden developer milestone tester.
// Hold the LOAD triangle for 10 seconds to enter/exit.
// While active: tap LOAD triangle = next milestone; SAVE triangle = previous.
static constexpr uint32_t DEV_LOAD_LONG_PRESS_MS = 10000;
bool developerTestMode = false;
int developerTestLevel = 1;
uint64_t developerSavedBoard[4][4] = {};
uint64_t developerSavedScore = 0;
uint64_t developerSavedBestScore = 0;
bool developerSavedGameOver = false;
int developerSavedBackgroundLevel = 0;


// Dedicated guard for CLOCK -> PHOTO long press.
// A stale/global touch sequence is never allowed to enter Album.
bool clockAlbumPressArmed = false;

// ============================================================
// APP MODE — GAME + ALBUM ONLY (v5 max-battery test)
// ============================================================

enum AppMode {
  MODE_GAME,
  MODE_CLOCK,
  MODE_SET_TIME,
  MODE_SET_ALARM,
  MODE_PHOTO,
  MODE_PHOTO_PICKER,
  MODE_ALARM_RINGING
};

AppMode appMode = MODE_GAME;

// Values edited on the manual RTC setting screen.
int editYear = 2026;
int editMonth = 1;
int editDay = 1;
int editHour = 12;
int editMinute = 0;

// Daily alarm.
int alarmHour = 7;
int alarmMinute = 30;
bool alarmEnabled = false;
int alarmVolume = 2;        // 0 = silent, 1..4 = louder
int editAlarmHour = 7;
int editAlarmMinute = 30;
int editAlarmVolume = 2;
int lastAlarmDateKey = -1;
uint32_t nextAlarmBeepMs = 0;
uint8_t alarmMelodyStep = 0;
AppMode alarmReturnMode = MODE_CLOCK;

Preferences alarmPrefs;
Preferences albumPrefs;
Preferences resumePrefs;

// selectedSaveSlot is defined in the SD/SAVE section below.
extern int selectedSaveSlot;

// ============================================================
// POWER-OFF RESUME CHECKPOINT
// ============================================================
// The PaperS3 side-button double-click is handled by its power controller.
// It can remove main power without giving the application a shutdown
// callback, so the safe strategy is to keep a tiny checkpoint CURRENT
// while the device is running.
//
// NVS is wear-levelled by ESP32 Preferences.  Only a small fixed record
// is written after a successful game-changing action.

static constexpr uint32_t RESUME_MAGIC = 0x52323034; // "R204"
static constexpr uint16_t RESUME_VERSION = 1;

struct ResumeGameState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint64_t board[4][4];
  uint64_t score;
  uint64_t bestScore;
  int32_t backgroundLevel;
  int32_t selectedSaveSlot;
  uint8_t gameOver;
};

void saveResumeGame() {
  ResumeGameState s = {};
  s.magic = RESUME_MAGIC;
  s.version = RESUME_VERSION;
  memcpy(s.board, board, sizeof(board));
  s.score = score;
  s.bestScore = bestScore;
  s.backgroundLevel = backgroundLevel;
  s.selectedSaveSlot = selectedSaveSlot;
  s.gameOver = gameOver ? 1 : 0;

  resumePrefs.begin("ripleyresume", false);
  resumePrefs.putBytes("game", &s, sizeof(s));
  resumePrefs.end();
}

bool loadResumeGame() {
  ResumeGameState s = {};

  resumePrefs.begin("ripleyresume", true);
  const size_t n = resumePrefs.getBytes("game", &s, sizeof(s));
  resumePrefs.end();

  if (n != sizeof(s) ||
      s.magic != RESUME_MAGIC ||
      s.version != RESUME_VERSION) {
    return false;
  }

  memcpy(board, s.board, sizeof(board));
  score = s.score;
  bestScore = s.bestScore;
  backgroundLevel = constrain((int)s.backgroundLevel, 0, 7);
  oldBackgroundLevel = backgroundLevel;
  selectedSaveSlot = constrain((int)s.selectedSaveSlot, 1, 3);
  gameOver = s.gameOver != 0;
  moveCount = 0;
  forceFullRefresh = true;
  return true;
}

void saveResumeMode(AppMode mode) {
  // Only persist the two user-facing states. Picker resumes into Album.
  uint8_t m = (mode == MODE_PHOTO || mode == MODE_PHOTO_PICKER) ? 1 : 0;
  resumePrefs.begin("ripleyresume", false);
  resumePrefs.putUChar("mode", m);
  resumePrefs.end();
}

uint8_t loadResumeMode() {
  resumePrefs.begin("ripleyresume", true);
  uint8_t m = resumePrefs.getUChar("mode", 0);
  resumePrefs.end();
  return m ? 1 : 0;
}

std::vector<String> albumFiles;
int albumIndex = -1;
int albumPickerPage = 0;
bool albumHasSavedPhoto = false;
uint32_t photoLastTapMs = 0;

// Cached values prevent unnecessary E-Ink updates.
int lastClockMinute = -1;
int lastStatusMinute = -1;
int lastStatusBattery = -999;

// Power-saving polling timestamps.
uint32_t lastAlarmCheckMs = 0;
uint32_t lastClockCheckMs = 0;

// ============================================================
// SD / SAVE
// ============================================================

bool sdReady = false;

bool mountSdIfNeeded() {
  if (sdReady) return true;

  sdReady =
    SD.begin(
      SD_CS,
      SPI,
      25000000
    );

  return sdReady;
}

void unmountSdForClock() {
  if (!sdReady) return;

  SD.end();
  sdReady = false;

  // Keep SPI initialized. Repeated SPI teardown/re-init is avoided.
}


int selectedSaveSlot = 1;

// ============================================================
// RIPLEY BACKGROUND RAW RGB565
// ============================================================
//
// Each 500 x 400 .bin file contains exactly 200,000 RGB565
// pixels = 400,000 bytes, little-endian.
//
// No PNG decoding is used.  The whole image is read from SD into
// PSRAM and pushed directly into M5GFX's framebuffer.
// ============================================================

static uint16_t *backgroundImage = nullptr;

bool ensureBackgroundImageBuffer() {
  if (backgroundImage) return true;

  const size_t bytes =
    (size_t)BACKGROUND_W * BACKGROUND_H * sizeof(uint16_t);

  backgroundImage = (uint16_t *)heap_caps_malloc(
    bytes,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  );

  if (!backgroundImage) {
    backgroundImage = (uint16_t *)heap_caps_malloc(
      bytes,
      MALLOC_CAP_8BIT
    );
  }

  return backgroundImage != nullptr;
}

// ============================================================
// HELPERS
// ============================================================

uint16_t gray565(uint8_t g) {
  return ((g & 0xF8) << 8) |
         ((g & 0xFC) << 3) |
         (g >> 3);
}

uint8_t getTileGray(uint64_t v) {
  if (v == 0)     return EMPTY_TILE_GRAY;
  if (v == 2)     return TILE_2_GRAY;
  if (v == 4)     return TILE_4_GRAY;
  if (v == 8)     return TILE_8_GRAY;
  if (v == 16)    return TILE_16_GRAY;
  if (v == 32)    return TILE_32_GRAY;
  if (v == 64)    return TILE_64_GRAY;
  if (v == 128)   return TILE_128_GRAY;
  if (v == 256)   return TILE_256_GRAY;
  if (v == 512)   return TILE_512_GRAY;
  if (v == 1024)  return TILE_1024_GRAY;
  if (v == 2048)  return TILE_2048_GRAY;
  if (v == 4096)  return TILE_4096_GRAY;
  if (v == 8192)  return TILE_8192_GRAY;
  if (v == 16384)  return TILE_16384_GRAY;
  if (v == 32768)  return TILE_32768_GRAY;
  if (v == 65536)  return TILE_65536_GRAY;
  if (v == 131072) return TILE_131072_GRAY;
  return TILE_131072_GRAY;
}

void printU64(uint64_t n) {
  char buf[32];

  snprintf(
    buf,
    sizeof(buf),
    "%llu",
    (unsigned long long)n
  );

  M5.Display.print(buf);
}

// ============================================================
// DRAW RIPLEY RAW RGB565 BACKGROUND
// ============================================================

bool drawBackgroundBIN(const char *filename) {

  if (!sdReady)
    return false;

  if (!SD.exists(filename))
    return false;

  if (!ensureBackgroundImageBuffer())
    return false;

  File f = SD.open(filename, FILE_READ);

  if (!f)
    return false;

  const size_t expectedBytes =
    (size_t)BACKGROUND_W * BACKGROUND_H * sizeof(uint16_t);

  if ((size_t)f.size() != expectedBytes) {
    f.close();
    return false;
  }

  uint8_t *dst =
    reinterpret_cast<uint8_t *>(backgroundImage);

  size_t totalRead = 0;

  while (totalRead < expectedBytes) {

    size_t n = f.read(
      dst + totalRead,
      expectedBytes - totalRead
    );

    if (n == 0)
      break;

    totalRead += n;
  }

  f.close();

  if (totalRead != expectedBytes)
    return false;

  // Use LovyanGFX's explicit RGB565 pixel type.
  // This avoids ambiguous uint16_t byte-order handling that made
  // the previous raw image appear as a nearly uniform gray mass.
  M5.Display.pushImage(
    BACKGROUND_X,
    BACKGROUND_Y,
    BACKGROUND_W,
    BACKGROUND_H,
    reinterpret_cast<lgfx::rgb565_t *>(backgroundImage)
  );

  return true;
}

bool drawEmbeddedBackground(int level) {
  level = constrain(level, 0, 7);

  const size_t expectedBytes =
    (size_t)BACKGROUND_W * BACKGROUND_H * sizeof(uint16_t);
  const uint8_t *src = BACKGROUND_DATA[level];
  const size_t bytes = (size_t)(BACKGROUND_END[level] - src);

  if (bytes != expectedBytes)
    return false;

  // Stage the complete embedded RGB565 image in the same writable RAM
  // buffer used by the mature SD path.  This keeps flash mapping / transfer
  // boundaries out of the E-Ink drawing path.
  if (!ensureBackgroundImageBuffer())
    return false;

  memcpy(
    reinterpret_cast<uint8_t *>(backgroundImage),
    src,
    expectedBytes
  );

  // v1.2.2: very slightly lighten ONLY the embedded Ripley game artwork.
  // Move each RGB565 channel ~4% toward white.  This is intentionally
  // subtle and leaves tiles/UI/Album/refresh behavior completely unchanged.
  for (size_t i = 0; i < (size_t)BACKGROUND_W * BACKGROUND_H; ++i) {
    uint16_t p = backgroundImage[i];
    uint16_t r = (p >> 11) & 0x1F;
    uint16_t g = (p >> 5)  & 0x3F;
    uint16_t b = p & 0x1F;

    r = min<uint16_t>(31, r + ((31 - r + 12) / 25));
    g = min<uint16_t>(63, g + ((63 - g + 12) / 25));
    b = min<uint16_t>(31, b + ((31 - b + 12) / 25));

    backgroundImage[i] = (uint16_t)((r << 11) | (g << 5) | b);
  }

  M5.Display.pushImage(
    BACKGROUND_X,
    BACKGROUND_Y,
    BACKGROUND_W,
    BACKGROUND_H,
    reinterpret_cast<lgfx::rgb565_t *>(backgroundImage)
  );

  return true;
}

void drawBackground() {

  if (backgroundLevel < 0)
    backgroundLevel = 0;

  if (backgroundLevel > 7)
    backgroundLevel = 7;

  // The full-game renderer already clears the entire framebuffer.
  // Do not paint a second white 500x400 rectangle immediately before the
  // artwork; on PaperS3 that extra pass can quantize differently and show up
  // as a horizontal/tonal seam inside a mostly-white illustration.
  drawEmbeddedBackground(backgroundLevel);

  if (BACKGROUND_LEFT_EDGE_MASK > 0) {

    M5.Display.fillRect(
      BACKGROUND_X,
      BACKGROUND_Y,
      BACKGROUND_LEFT_EDGE_MASK,
      BACKGROUND_H,
      gray565(UI_WHITE_GRAY)
    );
  }
}

// ============================================================
// GAME HELPERS
// ============================================================

void copyBoard(
  uint64_t source[4][4],
  uint64_t destination[4][4]
) {
  memcpy(
    destination,
    source,
    sizeof(board)
  );
}

void clearBoard() {
  memset(
    board,
    0,
    sizeof(board)
  );
}

uint64_t getMaximumTile() {

  uint64_t maximum = 0;

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {

      if (board[r][c] > maximum) {
        maximum = board[r][c];
      }
    }
  }

  return maximum;
}

int backgroundLevelForValue(uint64_t v) {

  if (v >= 131072) return 7;
  if (v >= 65536)  return 6;
  if (v >= 32768)  return 5;
  if (v >= 16384)  return 4;
  if (v >= 8192)  return 3;
  if (v >= 4096)  return 2;
  if (v >= 2048)  return 1;

  return 0;
}

void updateBackgroundLevel() {

  int reached =
    backgroundLevelForValue(
      getMaximumTile()
    );

  if (reached > backgroundLevel) {
    backgroundLevel = reached;
  }
}

// ============================================================
// HIDDEN DEVELOPER MILESTONE TESTER
// ============================================================

static uint64_t developerMilestoneValue(int level) {
  static const uint64_t values[8] = {
    0, 2048, 4096, 8192, 16384, 32768, 65536, 131072
  };
  return values[constrain(level, 0, 7)];
}

static void showDeveloperTestLevel(int level) {
  developerTestLevel = constrain(level, 0, 7);
  clearBoard();

  const uint64_t v = developerMilestoneValue(developerTestLevel);
  if (v != 0) board[1][1] = v;

  // A few small tiles make it easy to verify the board renderer too,
  // without creating merges or changing the user's real saved game.
  board[2][1] = 2;
  board[2][2] = 4;

  score = 0;
  gameOver = false;
  backgroundLevel = developerTestLevel;
  moveCount = 0;
  forceFullRefresh = true;
  drawFullGame();
}

static void enterDeveloperTestMode() {
  memcpy(developerSavedBoard, board, sizeof(board));
  developerSavedScore = score;
  developerSavedBestScore = bestScore;
  developerSavedGameOver = gameOver;
  developerSavedBackgroundLevel = backgroundLevel;
  developerTestMode = true;
  showDeveloperTestLevel(1);  // start at 2048 / bg1
}

static void exitDeveloperTestMode() {
  memcpy(board, developerSavedBoard, sizeof(board));
  score = developerSavedScore;
  bestScore = developerSavedBestScore;
  gameOver = developerSavedGameOver;
  backgroundLevel = developerSavedBackgroundLevel;
  oldBackgroundLevel = backgroundLevel;
  developerTestMode = false;
  moveCount = 0;
  forceFullRefresh = true;
  drawFullGame();
}

// ============================================================
// NEW GAME
// ============================================================

void addRandomTile();

void newGame() {

  clearBoard();

  score = 0;
  gameOver = false;
  moveCount = 0;

  backgroundLevel = 0;
  oldBackgroundLevel = 0;

  forceFullRefresh = true;

  addRandomTile();
  addRandomTile();
}

// ============================================================
// RANDOM TILE
// ============================================================

void addRandomTile() {

  int empty[16];
  int n = 0;

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {

      if (board[r][c] == 0) {
        empty[n++] = r * 4 + c;
      }
    }
  }

  if (n == 0) return;

  int p =
    empty[random(n)];

  board[p / 4][p % 4] =
    random(10) == 0
      ? 4
      : 2;
}

// ============================================================
// 2048 MERGE
// ============================================================

bool processLine(uint64_t v[4]) {

  uint64_t old[4] = {
    v[0], v[1], v[2], v[3]
  };

  uint64_t out[4] = {};
  bool merged[4] = {};

  int pos = 0;

  for (int i = 0; i < 4; ++i) {

    if (v[i] == 0) continue;

    if (
      pos > 0 &&
      out[pos - 1] == v[i] &&
      !merged[pos - 1]
    ) {

      uint64_t a = out[pos - 1];
      uint64_t b = v[i];

      out[pos - 1] =
        a > UINT64_MAX - b
          ? UINT64_MAX
          : a + b;

      score += out[pos - 1];

      merged[pos - 1] = true;

    } else {

      out[pos] = v[i];
      pos++;
    }
  }

  bool changed = false;

  for (int i = 0; i < 4; ++i) {

    if (old[i] != out[i]) {
      changed = true;
    }

    v[i] = out[i];
  }

  return changed;
}

// ============================================================
// MOVE
//
// 0 = UP
// 1 = RIGHT
// 2 = DOWN
// 3 = LEFT
// ============================================================

bool moveGame(int dir) {

  bool moved = false;

  for (int i = 0; i < 4; ++i) {

    uint64_t line[4];

    for (int j = 0; j < 4; ++j) {

      if (dir == 0) {
        line[j] = board[j][i];
      }

      if (dir == 1) {
        line[j] = board[i][3 - j];
      }

      if (dir == 2) {
        line[j] = board[3 - j][i];
      }

      if (dir == 3) {
        line[j] = board[i][j];
      }
    }

    if (processLine(line)) {
      moved = true;
    }

    for (int j = 0; j < 4; ++j) {

      if (dir == 0) {
        board[j][i] = line[j];
      }

      if (dir == 1) {
        board[i][3 - j] = line[j];
      }

      if (dir == 2) {
        board[3 - j][i] = line[j];
      }

      if (dir == 3) {
        board[i][j] = line[j];
      }
    }
  }

  return moved;
}

// ============================================================
// GAME OVER
// ============================================================

bool isGameOver() {

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {

      if (board[r][c] == 0) {
        return false;
      }

      if (
        c < 3 &&
        board[r][c] == board[r][c + 1]
      ) {
        return false;
      }

      if (
        r < 3 &&
        board[r][c] == board[r + 1][c]
      ) {
        return false;
      }
    }
  }

  return true;
}

// ============================================================
// BUTTON
// ============================================================

void drawButton(
  int x,
  int y,
  int w,
  int h,
  const char *label,
  bool selected = false
) {

  M5.Display.drawRoundRect(
    x,
    y,
    w,
    h,
    BUTTON_RADIUS,
    gray565(UI_BLACK_GRAY)
  );

  if (selected) {

    M5.Display.drawRoundRect(
      x + SELECTED_BORDER_INSET,
      y + SELECTED_BORDER_INSET,
      w - SELECTED_BORDER_INSET * 2,
      h - SELECTED_BORDER_INSET * 2,
      gray565(UI_BLACK_GRAY)
    );
  }

  M5.Display.setTextSize(
    SLOT_FONT_SIZE
  );

  int tw =
    M5.Display.textWidth(label);

  int th =
    M5.Display.fontHeight();

  M5.Display.setCursor(
    x + (w - tw) / 2,
    y + (h - th) / 2
  );

  M5.Display.print(label);
}

// ============================================================
// TILE
// ============================================================

void drawTile(int r, int c) {

  int x =
    BOARD_X +
    c * (TILE_SIZE + TILE_GAP);

  int y =
    BOARD_Y +
    r * (TILE_SIZE + TILE_GAP);

  uint64_t v =
    board[r][c];

  M5.Display.fillRoundRect(
    x,
    y,
    TILE_SIZE,
    TILE_SIZE,
    TILE_RADIUS,
    gray565(
      getTileGray(v)
    )
  );

  M5.Display.drawRoundRect(
    x,
    y,
    TILE_SIZE,
    TILE_SIZE,
    TILE_RADIUS,
    gray565(UI_BLACK_GRAY)
  );

  if (v == 0) return;

  char buf[32];

  snprintf(
    buf,
    sizeof(buf),
    "%llu",
    (unsigned long long)v
  );

  int textSize =
    TILE_FONT_MAX;

  while (textSize > 1) {

    M5.Display.setTextSize(
      textSize
    );

    int tw =
      M5.Display.textWidth(buf);

    int th =
      M5.Display.fontHeight();

    if (
      tw <= TILE_SIZE - TILE_TEXT_MARGIN &&
      th <= TILE_SIZE - TILE_TEXT_MARGIN
    ) {
      break;
    }

    textSize--;
  }

  M5.Display.setTextSize(
    textSize
  );

  int tw =
    M5.Display.textWidth(buf);

  int th =
    M5.Display.fontHeight();

  M5.Display.setCursor(
    x + (TILE_SIZE - tw) / 2,
    y + (TILE_SIZE - th) / 2
  );

  // Transparent text background
  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.print(buf);
}

// ============================================================
// SCORE / BEST
// ============================================================

void drawScoreCard(
  int x,
  const char *label,
  uint64_t value
) {

  // Clear only this compact score zone.
  M5.Display.fillRect(
    x,
    SCORE_CARD_Y,
    SCORE_CARD_W,
    SCORE_CARD_H,
    gray565(UI_WHITE_GRAY)
  );

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.setTextSize(
    SCORE_LABEL_SIZE
  );

  // SCORE label stays left-aligned.
  // BEST label is right-aligned to the same edge as its value.
  int labelX = x;

  if (
    strcmp(label, "BEST") == 0
  ) {
    int labelW =
      M5.Display.textWidth(label);

    labelX =
      x + SCORE_CARD_W - labelW;
  }

  M5.Display.setCursor(
    labelX,
    SCORE_CARD_Y
  );

  M5.Display.print(label);

  char buf[32];
  snprintf(
    buf,
    sizeof(buf),
    "%llu",
    (unsigned long long)value
  );

  int textSize =
    SCORE_VALUE_SIZE;

  while (textSize > 1) {
    M5.Display.setTextSize(textSize);

    if (
      M5.Display.textWidth(buf) <=
      SCORE_CARD_W
    ) {
      break;
    }

    textSize--;
  }

  M5.Display.setTextSize(
    textSize
  );

  int tw =
    M5.Display.textWidth(buf);

  // SCORE grows to the right; BEST aligns to the right edge.
  int valueX = x;

  if (
    strcmp(label, "BEST") == 0
  ) {
    valueX =
      x + SCORE_CARD_W - tw;
  }

  M5.Display.setCursor(
    valueX,
    SCORE_CARD_Y + 15
  );

  M5.Display.print(buf);
}

void drawScoreArea() {
  drawScoreCard(
    SCORE_CARD_X,
    "SCORE",
    score
  );
}

void drawBestArea() {
  drawScoreCard(
    BEST_CARD_X,
    "BEST",
    bestScore
  );
}

// ============================================================
// SMALL VECTOR ICONS
// ============================================================

void drawSaveIcon(int x, int y) {

  // SAVE: simple outline inverted triangle.
  // No shaft / tray — matches the requested game-screen symbol.
  const uint16_t ink =
    gray565(UI_BLACK_GRAY);

  const int s =
    SIDE_ICON_SIZE;

  const int left =
    x + 7;

  const int right =
    x + s - 7;

  const int top =
    y + 9;

  const int bottom =
    y + s - 6;

  const int cx =
    x + s / 2;

  // Double-line outline for cleaner E-Ink visibility.
  M5.Display.drawTriangle(
    left,
    top,
    right,
    top,
    cx,
    bottom,
    ink
  );

  M5.Display.drawTriangle(
    left + 2,
    top + 2,
    right - 2,
    top + 2,
    cx,
    bottom - 3,
    ink
  );
}

void drawLoadIcon(int x, int y) {

  // LOAD: exact screen-edge mirror of SAVE.
  // SAVE outer X = 17..53 on a 540 px portrait screen.
  // Therefore LOAD outer X = 487..523, leaving the same 17 px
  // gap to the right border as SAVE has to the left border.
  const uint16_t ink =
    gray565(UI_BLACK_GRAY);

  const int left   = W - 53;   // 487
  const int right  = W - 17;   // 523
  const int cx     = W - 35;   // 505

  // Vertically mirror the SAVE triangle.
  const int top    = y + 6;
  const int bottom = y + SIDE_ICON_SIZE - 9;

  M5.Display.drawTriangle(
    cx,
    top,
    left,
    bottom,
    right,
    bottom,
    ink
  );

  M5.Display.drawTriangle(
    cx,
    top + 3,
    left + 2,
    bottom - 2,
    right - 2,
    bottom - 2,
    ink
  );
}

void drawNewGameIcon(
  int x,
  int y,
  int w,
  int h
) {

  // NEW GAME: clear counter-clockwise ↶ style arrow.
  // Keep the same touch box; only redraw the visible icon.
  const uint16_t ink =
    gray565(UI_BLACK_GRAY);

  const int cx = x + w / 2;
  const int cy = y + h / 2 + 2;
  const int r  = 21;

  // Open arc: deliberately leave a large gap at upper-left.
  // Screen coordinates: +Y points downward.
  int lastX = 0;
  int lastY = 0;
  bool first = true;

  // From about 11 o'clock, travel clockwise around the right/bottom
  // to about 8 o'clock.  The arrow head at the first end points left,
  // making the overall symbol read as counter-clockwise.
  for (int deg = 235; deg <= 505; deg += 3) {
    float rad = deg * 0.01745329252f;

    int px =
      cx + (int)roundf(
        cosf(rad) * r
      );

    int py =
      cy + (int)roundf(
        sinf(rad) * r
      );

    if (!first) {
      M5.Display.drawLine(
        lastX,
        lastY,
        px,
        py,
        ink
      );

      M5.Display.drawLine(
        lastX,
        lastY + 1,
        px,
        py + 1,
        ink
      );
    }

    lastX = px;
    lastY = py;
    first = false;
  }

  // Large, unmistakable arrow head at upper-left.
  // Tip points left, like the user's pink reference.
  const int tipX = cx - 21;
  const int tipY = cy - 12;

  M5.Display.drawLine(
    tipX,
    tipY,
    tipX + 15,
    tipY - 7,
    ink
  );

  M5.Display.drawLine(
    tipX,
    tipY,
    tipX + 7,
    tipY + 15,
    ink
  );

  // 2 px visual weight for E-Ink.
  M5.Display.drawLine(
    tipX + 1,
    tipY + 1,
    tipX + 15,
    tipY - 6,
    ink
  );

  M5.Display.drawLine(
    tipX + 1,
    tipY + 1,
    tipX + 8,
    tipY + 15,
    ink
  );
}

// ============================================================
// STATIC UI
// ============================================================

void drawStaticUI() {

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  // Centered 2048 title.
  M5.Display.setTextSize(
    TITLE_SIZE
  );

  const char *title =
    "2048";

  int tw =
    M5.Display.textWidth(title);

  M5.Display.setCursor(
    (W - tw) / 2,
    TITLE_Y
  );

  M5.Display.print(title);

  // ----------------------------------------------------------
  // LEFT: SAVE inverted triangle + slots 1 / 2 / 3
  // ----------------------------------------------------------

  drawSaveIcon(
    SAVE_ICON_X,
    SIDE_ICON_Y
  );

  drawButton(
    SAVE_SLOT_X,
    SLOT_Y1,
    SLOT_W,
    SLOT_H,
    "1",
    selectedSaveSlot == 1
  );

  drawButton(
    SAVE_SLOT_X,
    SLOT_Y2,
    SLOT_W,
    SLOT_H,
    "2",
    selectedSaveSlot == 2
  );

  drawButton(
    SAVE_SLOT_X,
    SLOT_Y3,
    SLOT_W,
    SLOT_H,
    "3",
    selectedSaveSlot == 3
  );

  // ----------------------------------------------------------
  // RIGHT: LOAD upright triangle + slots 1 / 2 / 3
  // ----------------------------------------------------------

  drawLoadIcon(
    LOAD_ICON_X,
    SIDE_ICON_Y
  );

  drawButton(
    LOAD_SLOT_X,
    SLOT_Y1,
    SLOT_W,
    SLOT_H,
    "1"
  );

  drawButton(
    LOAD_SLOT_X,
    SLOT_Y2,
    SLOT_W,
    SLOT_H,
    "2"
  );

  drawButton(
    LOAD_SLOT_X,
    SLOT_Y3,
    SLOT_W,
    SLOT_H,
    "3"
  );

  // NEW GAME — icon only, below LOAD 3.
  drawNewGameIcon(
    NEW_X,
    NEW_Y,
    NEW_W,
    NEW_H
  );

}

// ============================================================
// GAME OVER WINDOW
// ============================================================

void drawGameOver() {

  if (!gameOver) return;

  M5.Display.fillRoundRect(
    GAMEOVER_X,
    GAMEOVER_Y,
    GAMEOVER_W,
    GAMEOVER_H,
    14,
    gray565(UI_WHITE_GRAY)
  );

  M5.Display.drawRoundRect(
    GAMEOVER_X,
    GAMEOVER_Y,
    GAMEOVER_W,
    GAMEOVER_H,
    14,
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.setTextSize(
    GAMEOVER_TITLE_SIZE
  );

  const char *msg =
    "GAME OVER";

  int tw =
    M5.Display.textWidth(msg);

  M5.Display.setCursor(
    (W - tw) / 2,
    GAMEOVER_TITLE_Y
  );

  M5.Display.print(msg);

  M5.Display.setTextSize(
    GAMEOVER_HINT_SIZE
  );

  const char *hint =
    "Tap to start";

  tw =
    M5.Display.textWidth(hint);

  M5.Display.setCursor(
    (W - tw) / 2,
    GAMEOVER_HINT_Y
  );

  M5.Display.print(hint);
}


// ============================================================
// FORWARD DECLARATIONS FOR APP / CLOCK MODE
// ============================================================

void drawFullGame();
void drawGame();
void drawStatusBar();
void drawClockMode();
void drawRtcSettingScreen();
void drawAlarmSettingScreen();
void drawAlarmRingingScreen();
int largestClockTextSize(const char *text);
void returnToGameMode();
void deepCleanEpd();
bool drawCurrentAlbumPhoto();
static inline void holdPowerAwakeForPanel();
static inline bool eventIdleLightSleep(uint32_t nowMs);

// ============================================================
// STRONG E-INK CLEAN
//
// Used only for mode changes / manual refresh.
// First physically refreshes a full white screen using the
// image-quality waveform.  This greatly reduces retained game
// tiles and old clock digits before the new mode is drawn.
// ============================================================

void deepCleanEpd() {

  M5.Display.setEpdMode(
    epd_mode_t::epd_quality
  );

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  M5.Display.display();

  delay(80);

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );
}

// ============================================================
// RTC / CLOCK HELPERS
// ============================================================

bool pointInRect(
  int x,
  int y,
  int rx,
  int ry,
  int rw,
  int rh
) {
  return
    x >= rx &&
    x < rx + rw &&
    y >= ry &&
    y < ry + rh;
}

bool isLeapYear(int year) {
  return
    (year % 400 == 0) ||
    ((year % 4 == 0) && (year % 100 != 0));
}

int daysInMonth(int year, int month) {

  static const uint8_t days[12] = {
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
  };

  if (month < 1 || month > 12)
    return 31;

  if (month == 2 && isLeapYear(year))
    return 29;

  return days[month - 1];
}

void clampEditDate() {

  if (editYear < 2000) editYear = 2000;
  if (editYear > 2099) editYear = 2099;

  if (editMonth < 1) editMonth = 12;
  if (editMonth > 12) editMonth = 1;

  int maxDay =
    daysInMonth(
      editYear,
      editMonth
    );

  if (editDay < 1)
    editDay = maxDay;

  if (editDay > maxDay)
    editDay = 1;

  if (editHour < 0) editHour = 23;
  if (editHour > 23) editHour = 0;

  if (editMinute < 0) editMinute = 59;
  if (editMinute > 59) editMinute = 0;
}

bool rtcDateTimeLooksValid() {

  if (!M5.Rtc.isEnabled())
    return false;

  auto dt =
    M5.Rtc.getDateTime();

  if (dt.date.year < 2000 ||
      dt.date.year > 2099)
    return false;

  if (dt.date.month < 1 ||
      dt.date.month > 12)
    return false;

  if (dt.date.date < 1 ||
      dt.date.date >
        daysInMonth(
          dt.date.year,
          dt.date.month
        ))
    return false;

  if (dt.time.hours > 23 ||
      dt.time.minutes > 59)
    return false;

  return true;
}

void loadRtcIntoEditor() {

  if (rtcDateTimeLooksValid()) {

    auto dt =
      M5.Rtc.getDateTime();

    editYear =
      dt.date.year;

    editMonth =
      dt.date.month;

    editDay =
      dt.date.date;

    editHour =
      dt.time.hours;

    editMinute =
      dt.time.minutes;

  } else {

    // Safe starting point if the RTC has never been set.
    editYear = 2026;
    editMonth = 1;
    editDay = 1;
    editHour = 12;
    editMinute = 0;
  }

  clampEditDate();
}

void saveEditorToRtc() {

  clampEditDate();

  if (!M5.Rtc.isEnabled())
    return;

  M5.Rtc.setDateTime(
    {
      {
        editYear,
        editMonth,
        editDay
      },
      {
        editHour,
        editMinute,
        0
      }
    }
  );
}


// ============================================================
// ALARM HELPERS
// ============================================================

void loadAlarmSettings() {

  alarmPrefs.begin("ripley2048", true);

  alarmEnabled =
    alarmPrefs.getBool("alarmOn", false);

  alarmHour =
    alarmPrefs.getInt("alarmHour", 7);

  alarmMinute =
    alarmPrefs.getInt("alarmMin", 30);

  alarmVolume =
    alarmPrefs.getInt("alarmVol", 2);

  alarmPrefs.end();

  if (alarmHour < 0 || alarmHour > 23)
    alarmHour = 7;

  if (alarmMinute < 0 || alarmMinute > 59)
    alarmMinute = 30;

  if (alarmVolume < 0 || alarmVolume > 4)
    alarmVolume = 2;
}

void saveAlarmSettings() {

  alarmPrefs.begin("ripley2048", false);

  alarmPrefs.putBool(
    "alarmOn",
    alarmEnabled
  );

  alarmPrefs.putInt(
    "alarmHour",
    alarmHour
  );

  alarmPrefs.putInt(
    "alarmMin",
    alarmMinute
  );

  alarmPrefs.putInt(
    "alarmVol",
    alarmVolume
  );

  alarmPrefs.end();
}

void clampAlarmEditor() {

  if (editAlarmHour < 0)
    editAlarmHour = 23;

  if (editAlarmHour > 23)
    editAlarmHour = 0;

  if (editAlarmMinute < 0)
    editAlarmMinute = 59;

  if (editAlarmMinute > 59)
    editAlarmMinute = 0;

  if (editAlarmVolume < 0)
    editAlarmVolume = 4;

  if (editAlarmVolume > 4)
    editAlarmVolume = 0;
}

int rtcDateKey() {

  if (!rtcDateTimeLooksValid())
    return -1;

  auto dt = M5.Rtc.getDateTime();

  return
    dt.date.year * 10000 +
    dt.date.month * 100 +
    dt.date.date;
}

void drawAlarmRingingScreen() {

  deepCleanEpd();

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  // Dedicated Ripley wake-up artwork.  It uses the same exact
  // 500 x 400 RGB565 format as bg0..bg5.
  drawBackgroundBIN(ALARM_IMAGE_FILE);

  if (BACKGROUND_LEFT_EDGE_MASK > 0) {
    M5.Display.fillRect(
      BACKGROUND_X,
      BACKGROUND_Y,
      BACKGROUND_LEFT_EDGE_MASK,
      BACKGROUND_H,
      gray565(UI_WHITE_GRAY)
    );
  }

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  // Keep the rest of the alarm screen very clean.
  M5.Display.setTextSize(3);
  const char *hint = "TAP TO STOP";
  int tw = M5.Display.textWidth(hint);

  M5.Display.setCursor(
    (W - tw) / 2,
    835
  );

  M5.Display.print(hint);

  M5.Display.display();

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );
}

void startAlarm() {

  if (appMode == MODE_ALARM_RINGING)
    return;

  alarmReturnMode = MODE_CLOCK;

  appMode = MODE_ALARM_RINGING;

  // Never let a touch sequence from the previous mode survive into
  // the alarm screen.
  touchTracking = false;
  clockAlbumPressArmed = false;

  // Alarm artwork/UI is portrait even if it interrupted the album.
  M5.Display.setRotation(0);

  lastAlarmDateKey = rtcDateKey();
  nextAlarmBeepMs = 0;
  alarmMelodyStep = 0;

  // Alarm artwork lives on SD. CLOCK may have unmounted it.
  mountSdIfNeeded();

  // Power the speaker only while the alarm is actually ringing.
  M5.Speaker.begin();
  M5.Speaker.stop();

  drawAlarmRingingScreen();
}

void stopAlarm() {

  M5.Speaker.stop();
  M5.Speaker.end();

  touchTracking = false;
  clockAlbumPressArmed = false;

  // Returning to CLOCK; release SD again.
  unmountSdForClock();

  M5.Display.setRotation(1);
  appMode = MODE_CLOCK;
  lastClockMinute = -999;

  deepCleanEpd();
  drawClockMode();
}

void updateAlarmSound() {

  if (appMode != MODE_ALARM_RINGING)
    return;

  if (alarmVolume <= 0)
    return;

  uint32_t now = millis();

  if ((int32_t)(now - nextAlarmBeepMs) < 0)
    return;

  // Four simple loudness levels. M5Unified speaker volume is 0..255.
  static const uint8_t VOLUME_TABLE[5] = {
    0, 64, 128, 192, 255
  };

  // Six-note X-Files-inspired alarm motif requested by the user:
  // A - E - D - E - G - E
  //
  // Keep it monophonic and slightly spacious so the small PaperS3
  // buzzer retains the eerie, whistled contour instead of sounding
  // like six rapid notification beeps.
  static const uint16_t MELODY_HZ[6] = {
    440, 659, 587, 659, 784, 659
  };

  static const uint16_t NOTE_MS[6] = {
    360, 360, 360, 360, 360, 520
  };

  static const uint16_t STEP_MS[6] = {
    520, 520, 520, 520, 520, 1250
  };

  M5.Speaker.setVolume(
    VOLUME_TABLE[alarmVolume]
  );

  M5.Speaker.tone(
    MELODY_HZ[alarmMelodyStep],
    NOTE_MS[alarmMelodyStep]
  );

  nextAlarmBeepMs =
    now + STEP_MS[alarmMelodyStep];

  alarmMelodyStep++;

  if (alarmMelodyStep >= 6)
    alarmMelodyStep = 0;
}

void checkAlarmTrigger() {

  if (!alarmEnabled)
    return;

  if (appMode == MODE_ALARM_RINGING)
    return;

  if (!rtcDateTimeLooksValid())
    return;

  auto dt = M5.Rtc.getDateTime();

  int dateKey =
    dt.date.year * 10000 +
    dt.date.month * 100 +
    dt.date.date;

  if (dateKey == lastAlarmDateKey)
    return;

  if (
    dt.time.hours == alarmHour &&
    dt.time.minutes == alarmMinute
  ) {
    startAlarm();
  }
}

// ============================================================
// GAME MODE STATUS BAR
// ============================================================

void drawStatusBar() {

  M5.Display.fillRect(
    0,
    STATUS_BAR_Y,
    W,
    STATUS_BAR_H,
    gray565(UI_WHITE_GRAY)
  );

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.setTextSize(
    STATUS_FONT_SIZE
  );

  int battery =
    M5.Power.getBatteryLevel();

  char left[32];

  if (battery >= 0 && battery <= 100) {

    snprintf(
      left,
      sizeof(left),
      "BAT %d%%",
      battery
    );

  } else {

    snprintf(
      left,
      sizeof(left),
      "BAT --"
    );
  }

  int th =
    M5.Display.fontHeight();

  M5.Display.setCursor(
    20,
    STATUS_BAR_Y +
      (STATUS_BAR_H - th) / 2
  );

  M5.Display.print(left);

  // v5.1 GAME+ALBUM build:
  // No clock / RTC status is drawn on the right side.
  // Leave that area completely blank.
  lastStatusMinute = -1;
  lastStatusBattery = battery;
}

void updateGameStatusBarIfNeeded() {
  // Intentionally disabled in idle GAME mode.
  //
  // The status bar is refreshed only when the game is already
  // redrawing because of a move / save / load / full refresh.
  // This prevents the bottom time/battery line from flashing
  // by itself every minute.
  return;
}

// ============================================================
// CLOCK MODE
// ============================================================

int largestClockTextSize(
  const char *text
) {

  for (int size = CLOCK_FONT_MAX;
       size >= 1;
       --size) {

    M5.Display.setTextSize(size);

    if (
      M5.Display.textWidth(text) <=
        CLOCK_W - (CLOCK_TIME_SIDE_MARGIN * 2) &&
      M5.Display.fontHeight() <= CLOCK_TIME_H
    ) {

      return size;
    }
  }

  return 1;
}

void drawClockTimeOnly(
  bool refreshPanel
) {

  M5.Display.fillRect(
    0,
    CLOCK_TIME_TOP,
    CLOCK_W,
    CLOCK_TIME_H,
    gray565(UI_WHITE_GRAY)
  );

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  char timeText[16];

  if (rtcDateTimeLooksValid()) {

    auto dt =
      M5.Rtc.getDateTime();

    snprintf(
      timeText,
      sizeof(timeText),
      "%02d:%02d",
      dt.time.hours,
      dt.time.minutes
    );

    lastClockMinute =
      dt.time.minutes;

  } else {

    snprintf(
      timeText,
      sizeof(timeText),
      "--:--"
    );

    lastClockMinute = -1;
  }

  int size =
    largestClockTextSize(
      timeText
    );

  M5.Display.setTextSize(size);

  int tw =
    M5.Display.textWidth(
      timeText
    );

  int th =
    M5.Display.fontHeight();

  M5.Display.setCursor(
    (CLOCK_W - tw) / 2,
    CLOCK_TIME_TOP +
      (CLOCK_TIME_H - th) / 2
  );

  M5.Display.print(
    timeText
  );

  if (refreshPanel) {

    M5.Display.setEpdMode(
      epd_mode_t::epd_text
    );
    M5.Display.display();
    M5.Display.setEpdMode(
      epd_mode_t::epd_fastest
    );
  }
}

void drawClockMode() {

  // Clock is always landscape.
  M5.Display.setRotation(1);

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  drawClockTimeOnly(false);

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  // Date.
  char dateText[32];

  if (rtcDateTimeLooksValid()) {

    auto dt =
      M5.Rtc.getDateTime();

    snprintf(
      dateText,
      sizeof(dateText),
      "%04d-%02d-%02d",
      dt.date.year,
      dt.date.month,
      dt.date.date
    );

  } else {

    snprintf(
      dateText,
      sizeof(dateText),
      "SET TIME"
    );
  }

  M5.Display.setTextSize(
    CLOCK_DATE_SIZE
  );

  int tw =
    M5.Display.textWidth(
      dateText
    );

  M5.Display.setCursor(
    (CLOCK_W - tw) / 2,
    CLOCK_DATE_Y
  );

  M5.Display.print(
    dateText
  );

  // Alarm status — left side.
  char alarmText[40];

  if (alarmEnabled) {
    snprintf(alarmText, sizeof(alarmText), "ALARM %02d:%02d  VOL %d", alarmHour, alarmMinute, alarmVolume);
  } else {
    snprintf(alarmText, sizeof(alarmText), "ALARM OFF");
  }

  M5.Display.setTextSize(CLOCK_ALARM_STATUS_SIZE);
  M5.Display.setCursor(CLOCK_STATUS_SIDE_MARGIN, CLOCK_STATUS_Y);
  M5.Display.print(alarmText);

  // Battery — right side.
  int battery = M5.Power.getBatteryLevel();
  char batteryText[24];
  if (battery >= 0 && battery <= 100) {
    snprintf(batteryText, sizeof(batteryText), "BAT %d%%", battery);
  } else {
    snprintf(batteryText, sizeof(batteryText), "BAT --");
  }
  M5.Display.setTextSize(CLOCK_BATTERY_SIZE);
  tw = M5.Display.textWidth(batteryText);
  M5.Display.setCursor(CLOCK_W - CLOCK_STATUS_SIDE_MARGIN - tw, CLOCK_STATUS_Y);
  M5.Display.print(batteryText);

  drawButton(
    CLOCK_SET_X,
    CLOCK_SET_Y,
    CLOCK_SET_W,
    CLOCK_SET_H,
    "TIME"
  );

  drawButton(
    CLOCK_ALARM_X,
    CLOCK_ALARM_Y,
    CLOCK_ALARM_W,
    CLOCK_ALARM_H,
    "ALARM"
  );

  M5.Display.setEpdMode(
    epd_mode_t::epd_text
  );

  M5.Display.display();

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );
}

void updateClockModeIfNeeded() {

  if (appMode != MODE_CLOCK)
    return;

  int minute = -1;

  if (rtcDateTimeLooksValid()) {
    auto dt = M5.Rtc.getDateTime();
    minute = dt.time.minutes;
  }

  if (minute == lastClockMinute)
    return;

  // Full clock redraw once per minute.
  // This keeps date / battery / SET button intact and avoids
  // the partial-refresh clipping seen on PaperS3.
  drawClockMode();

  // A clock redraw can continue driving the E-Ink panel after display().
  // Keep the MCU fully awake long enough for the waveform to settle.
  powerAwakeUntilMs = millis() + POWER_SLEEP_GRACE_MS;
}

// ============================================================
// RTC SETTING SCREEN
// ============================================================

void drawSmallControlButton(
  int x,
  int y,
  const char *label
) {

  drawButton(
    x,
    y,
    RTC_SET_BUTTON_W,
    RTC_SET_BUTTON_H,
    label
  );
}

void drawRtcSettingScreen() {

  clampEditDate();

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.setTextSize(4);

  const char *title =
    "SET TIME";

  int tw =
    M5.Display.textWidth(
      title
    );

  M5.Display.setCursor(
    (W - tw) / 2,
    RTC_SET_TITLE_Y
  );

  M5.Display.print(title);

  // HH : MM
  char timeValue[16];

  snprintf(
    timeValue,
    sizeof(timeValue),
    "%02d : %02d",
    editHour,
    editMinute
  );

  M5.Display.setTextSize(
    RTC_SET_VALUE_SIZE
  );

  tw =
    M5.Display.textWidth(
      timeValue
    );

  M5.Display.setCursor(
    (W - tw) / 2,
    180
  );

  M5.Display.print(
    timeValue
  );

  // Hour +/- and minute +/-.
  drawSmallControlButton(105, 245, "+");
  drawSmallControlButton(105, 315, "-");

  drawSmallControlButton(365, 245, "+");
  drawSmallControlButton(365, 315, "-");

  M5.Display.setTextSize(2);
  M5.Display.setCursor(112, 380);
  M5.Display.print("HOUR");

  M5.Display.setCursor(355, 380);
  M5.Display.print("MINUTE");

  // YYYY-MM-DD
  char dateValue[24];

  snprintf(
    dateValue,
    sizeof(dateValue),
    "%04d-%02d-%02d",
    editYear,
    editMonth,
    editDay
  );

  M5.Display.setTextSize(4);

  tw =
    M5.Display.textWidth(
      dateValue
    );

  M5.Display.setCursor(
    (W - tw) / 2,
    470
  );

  M5.Display.print(
    dateValue
  );

  // Year +/-.
  drawSmallControlButton(30, 540, "+");
  drawSmallControlButton(30, 610, "-");

  // Month +/-.
  drawSmallControlButton(235, 540, "+");
  drawSmallControlButton(235, 610, "-");

  // Day +/-.
  drawSmallControlButton(440, 540, "+");
  drawSmallControlButton(440, 610, "-");

  M5.Display.setTextSize(2);

  M5.Display.setCursor(29, 680);
  M5.Display.print("YEAR");

  M5.Display.setCursor(226, 680);
  M5.Display.print("MONTH");

  M5.Display.setCursor(451, 680);
  M5.Display.print("DAY");

  // SAVE / CANCEL
  drawButton(
    70,
    790,
    180,
    65,
    "SAVE"
  );

  drawButton(
    290,
    790,
    180,
    65,
    "CANCEL"
  );

  M5.Display.setEpdMode(
    epd_mode_t::epd_quality
  );

  M5.Display.display();

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );
}

void enterClockMode() {

  M5.Display.setRotation(1);

  appMode =
    MODE_CLOCK;

  // Clear any touch state left over from GAME or a previous rotation.
  touchTracking = false;
  clockAlbumPressArmed = false;

  lastClockMinute = -999;

  // CLOCK itself does not need SD. Unmount the card but keep SPI alive.
  unmountSdForClock();

  // Erase the game image physically before drawing the clock.
  deepCleanEpd();

  drawClockMode();
}

void returnToGameMode() {

  mountSdIfNeeded();

  M5.Display.setRotation(0);

  appMode =
    MODE_GAME;

  forceFullRefresh =
    true;

  lastStatusMinute = -999;
  lastStatusBattery = -999;

  // Erase the large clock digits physically before restoring
  // the game screen.
  deepCleanEpd();

  drawFullGame();
}

void enterRtcSettingMode() {

  M5.Display.setRotation(0);
  loadRtcIntoEditor();

  appMode =
    MODE_SET_TIME;

  touchTracking = false;
  clockAlbumPressArmed = false;

  drawRtcSettingScreen();
}

void handleRtcSettingTap(
  int x,
  int y
) {

  bool changed =
    false;

  // Hour +
  if (pointInRect(
        x, y,
        105, 245,
        RTC_SET_BUTTON_W,
        RTC_SET_BUTTON_H)) {

    editHour++;
    changed = true;
  }

  // Hour -
  else if (pointInRect(
             x, y,
             105, 315,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editHour--;
    changed = true;
  }

  // Minute +
  else if (pointInRect(
             x, y,
             365, 245,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editMinute++;
    changed = true;
  }

  // Minute -
  else if (pointInRect(
             x, y,
             365, 315,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editMinute--;
    changed = true;
  }

  // Year +
  else if (pointInRect(
             x, y,
             30, 540,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editYear++;
    changed = true;
  }

  // Year -
  else if (pointInRect(
             x, y,
             30, 610,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editYear--;
    changed = true;
  }

  // Month +
  else if (pointInRect(
             x, y,
             235, 540,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editMonth++;
    changed = true;
  }

  // Month -
  else if (pointInRect(
             x, y,
             235, 610,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editMonth--;
    changed = true;
  }

  // Day +
  else if (pointInRect(
             x, y,
             440, 540,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editDay++;
    changed = true;
  }

  // Day -
  else if (pointInRect(
             x, y,
             440, 610,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editDay--;
    changed = true;
  }

  // SAVE
  else if (pointInRect(
             x, y,
             70, 790,
             180, 65)) {

    saveEditorToRtc();

    M5.Display.setRotation(1);

    appMode =
      MODE_CLOCK;

    lastClockMinute = -999;

    deepCleanEpd();
    drawClockMode();

    return;
  }

  // CANCEL
  else if (pointInRect(
             x, y,
             290, 790,
             180, 65)) {

    M5.Display.setRotation(1);

    appMode =
      MODE_CLOCK;

    lastClockMinute = -999;

    deepCleanEpd();
    drawClockMode();

    return;
  }

  if (changed) {

    clampEditDate();

    drawRtcSettingScreen();
  }
}


// ============================================================
// ALARM SETTING SCREEN
// ============================================================

void drawAlarmSettingScreen() {

  clampAlarmEditor();

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.setTextSize(4);

  const char *title = "SET ALARM";
  int tw = M5.Display.textWidth(title);

  M5.Display.setCursor(
    (W - tw) / 2,
    ALARM_SET_TITLE_Y
  );

  M5.Display.print(title);

  char value[16];
  snprintf(
    value,
    sizeof(value),
    "%02d : %02d",
    editAlarmHour,
    editAlarmMinute
  );

  M5.Display.setTextSize(
    ALARM_SET_VALUE_SIZE
  );

  tw = M5.Display.textWidth(value);

  M5.Display.setCursor(
    (W - tw) / 2,
    ALARM_SET_VALUE_Y
  );

  M5.Display.print(value);

  drawSmallControlButton(105, 365, "+");
  drawSmallControlButton(105, 435, "-");

  drawSmallControlButton(365, 365, "+");
  drawSmallControlButton(365, 435, "-");

  M5.Display.setTextSize(2);
  M5.Display.setCursor(112, 500);
  M5.Display.print("HOUR");

  M5.Display.setCursor(355, 500);
  M5.Display.print("MINUTE");

  // Alarm volume: 0 = silent, 1..4 = louder.
  M5.Display.setTextSize(2);
  M5.Display.setCursor(85, 575);
  M5.Display.print("VOLUME");

  char volumeText[8];
  snprintf(
    volumeText,
    sizeof(volumeText),
    "%d",
    editAlarmVolume
  );

  M5.Display.setTextSize(4);
  tw = M5.Display.textWidth(volumeText);
  M5.Display.setCursor(
    (W - tw) / 2,
    565
  );
  M5.Display.print(volumeText);

  drawSmallControlButton(365, 555, "+");
  drawSmallControlButton(365, 625, "-");

  drawButton(
    170,
    660,
    200,
    65,
    alarmEnabled ? "ON" : "OFF",
    alarmEnabled
  );

  drawButton(
    70,
    790,
    180,
    65,
    "SAVE"
  );

  drawButton(
    290,
    790,
    180,
    65,
    "CANCEL"
  );

  M5.Display.setEpdMode(
    epd_mode_t::epd_quality
  );

  M5.Display.display();

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );
}

void enterAlarmSettingMode() {

  M5.Display.setRotation(0);

  editAlarmHour = alarmHour;
  editAlarmMinute = alarmMinute;
  editAlarmVolume = alarmVolume;

  appMode = MODE_SET_ALARM;

  drawAlarmSettingScreen();
}

void handleAlarmSettingTap(
  int x,
  int y
) {

  bool changed = false;

  if (pointInRect(
        x, y,
        105, 365,
        RTC_SET_BUTTON_W,
        RTC_SET_BUTTON_H)) {

    editAlarmHour++;
    changed = true;
  }
  else if (pointInRect(
             x, y,
             105, 435,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editAlarmHour--;
    changed = true;
  }
  else if (pointInRect(
             x, y,
             365, 365,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editAlarmMinute++;
    changed = true;
  }
  else if (pointInRect(
             x, y,
             365, 435,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editAlarmMinute--;
    changed = true;
  }
  else if (pointInRect(
             x, y,
             365, 555,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editAlarmVolume++;
    changed = true;
  }
  else if (pointInRect(
             x, y,
             365, 625,
             RTC_SET_BUTTON_W,
             RTC_SET_BUTTON_H)) {

    editAlarmVolume--;
    changed = true;
  }
  else if (pointInRect(
             x, y,
             170, 660,
             200, 65)) {

    alarmEnabled = !alarmEnabled;
    changed = true;
  }
  else if (pointInRect(
             x, y,
             70, 790,
             180, 65)) {

    clampAlarmEditor();

    alarmHour = editAlarmHour;
    alarmMinute = editAlarmMinute;
    alarmVolume = editAlarmVolume;

    // Allow a newly changed alarm to fire today if its time has
    // not already been handled with the new setting.
    lastAlarmDateKey = -1;

    saveAlarmSettings();

    M5.Display.setRotation(1);
    appMode = MODE_CLOCK;
    lastClockMinute = -999;

    deepCleanEpd();
    drawClockMode();
    return;
  }
  else if (pointInRect(
             x, y,
             290, 790,
             180, 65)) {

    // Reload the stored value so cancelling also cancels an
    // unsaved ON/OFF toggle.
      M5.Display.setRotation(1);
    appMode = MODE_CLOCK;
    lastClockMinute = -999;

    deepCleanEpd();
    drawClockMode();
    return;
  }

  if (changed) {
    clampAlarmEditor();
    drawAlarmSettingScreen();
  }
}

// ============================================================
// FULL DRAW
// ============================================================

void drawFullGame() {

  // IMPORTANT for PaperS3: select the quality waveform BEFORE composing the
  // framebuffer.  M5GFX's E-Ink rendering/quantization can depend on the
  // active EPD mode while pixels are written.  Drawing in epd_fastest and
  // switching to epd_quality only at display() can leave two slightly
  // different white/gray regions in one 500x400 image.
  M5.Display.setEpdMode(
    epd_mode_t::epd_quality
  );

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  drawBackground();
  drawStaticUI();
  drawScoreArea();
  drawBestArea();

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      drawTile(r, c);
    }
  }

  drawGameOver();
  drawStatusBar();

  // Commit the already-quality-composed framebuffer in one full refresh.
  M5.Display.display();

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );

  forceFullRefresh = false;
}

// ============================================================
// PARTIAL DRAW
// ============================================================

void updateChangedTiles() {

  if (
    oldBackgroundLevel !=
    backgroundLevel
  ) {

    forceFullRefresh = true;
  }

  if (forceFullRefresh) {

    drawFullGame();
    return;
  }

  bool changed = false;

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {

      if (
        oldBoard[r][c] !=
        board[r][c]
      ) {

        drawTile(r, c);
        changed = true;
      }
    }
  }

  if (oldScore != score) {

    drawScoreArea();
    changed = true;
  }

  if (oldBestScore != bestScore) {

    drawBestArea();
    changed = true;
  }

  if (oldGameOver != gameOver) {

    drawFullGame();
    return;
  }

  if (changed) {

    // Update BAT + TIME only when the game is already drawing.
    // No independent minute-based E-Ink refresh in GAME mode.
    drawStatusBar();

    M5.Display.display();
  }
}

void drawGame() {

  if (forceFullRefresh) {
    drawFullGame();
  } else {
    updateChangedTiles();
  }
}

// ============================================================
// SAVE
//
// 4 board lines
// score
// best
// backgroundLevel
// ============================================================

struct UserSaveState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint64_t board[4][4];
  uint64_t score;
  uint64_t bestScore;
  int32_t backgroundLevel;
};

static const char *slotKey(int slot) {
  static const char *keys[3] = {"slot1", "slot2", "slot3"};
  return (slot >= 1 && slot <= 3) ? keys[slot - 1] : nullptr;
}

bool hasGameSlot(int slot) {
  const char *key = slotKey(slot);
  if (!key) return false;
  Preferences p;
  p.begin("ripley2048", true);
  size_t n = p.getBytesLength(key);
  p.end();
  return n == sizeof(UserSaveState);
}

void saveGame(int slot) {
  const char *key = slotKey(slot);
  if (!key) return;
  UserSaveState s = {};
  s.magic = RESUME_MAGIC;
  s.version = RESUME_VERSION;
  memcpy(s.board, board, sizeof(board));
  s.score = score;
  s.bestScore = bestScore;
  s.backgroundLevel = backgroundLevel;

  Preferences p;
  p.begin("ripley2048", false);
  p.putBytes(key, &s, sizeof(s));
  p.end();
  selectedSaveSlot = slot;
}

void loadGame(int slot) {
  const char *key = slotKey(slot);
  if (!key) return;
  UserSaveState s = {};
  Preferences p;
  p.begin("ripley2048", true);
  size_t n = p.getBytes(key, &s, sizeof(s));
  p.end();
  if (n != sizeof(s) || s.magic != RESUME_MAGIC || s.version != RESUME_VERSION) return;

  memcpy(board, s.board, sizeof(board));
  score = s.score;
  bestScore = s.bestScore;
  selectedSaveSlot = slot;
  gameOver = isGameOver();
  moveCount = 0;
  int boardLevel = backgroundLevelForValue(getMaximumTile());
  backgroundLevel = constrain((int)s.backgroundLevel, 0, 7);
  if (boardLevel > backgroundLevel) backgroundLevel = boardLevel;
  oldBackgroundLevel = backgroundLevel;
  forceFullRefresh = true;
  saveResumeGame();
}

// ============================================================
// PHOTO ALBUM
// ============================================================

bool isAlbumImageFile(const String &name) {
  String n = name;
  n.toLowerCase();
  return n.endsWith(".jpg") ||
         n.endsWith(".jpeg") ||
         n.endsWith(".png");
}

String albumBaseName(const String &path) {
  int slash = path.lastIndexOf('/');
  return (slash >= 0) ? path.substring(slash + 1) : path;
}

void scanAlbumFolder() {
  albumFiles.clear();

  if (!sdReady) {
    return;
  }

  // ESP32 File/SD implementations are not perfectly consistent about
  // whether directory paths/names include a leading slash. Try both.
  File dir = SD.open("/album");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    dir = SD.open("album");
  }

  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String rawName = String(f.name());
      String testName = rawName;
      testName.toLowerCase();

      if (testName.endsWith(".jpg") ||
          testName.endsWith(".jpeg") ||
          testName.endsWith(".png")) {

        // f.name() may return:
        //   IMG_001.JPG
        //   /IMG_001.JPG
        //   album/IMG_001.JPG
        //   /album/IMG_001.JPG
        // Normalize every case to /album/<filename>.
        String base = rawName;
        int slash = base.lastIndexOf('/');
        if (slash >= 0) {
          base = base.substring(slash + 1);
        }

        // Ignore hidden/system files.
        // This specifically prevents macOS AppleDouble files such as
        // "._photo.jpg" from being mistaken for real JPEG images.
        if (base.length() > 0 &&
            !base.startsWith(".")) {
          albumFiles.push_back(
            String("/album/") + base
          );
        }
      }
    }

    f.close();
    f = dir.openNextFile();
  }

  dir.close();

  std::sort(
    albumFiles.begin(),
    albumFiles.end(),
    [](const String &a, const String &b) {
      String aa = a;
      String bb = b;
      aa.toLowerCase();
      bb.toLowerCase();
      return aa.compareTo(bb) < 0;
    }
  );
}

// Re-read /album while preserving the photo currently on screen whenever
// possible.  This lets photos copied to the microSD after boot appear
// immediately, without requiring a reboot or leaving Album mode.
void rescanAlbumPreserveCurrent() {
  String current = "";

  if (albumIndex >= 0 &&
      albumIndex < (int)albumFiles.size()) {
    current = albumFiles[albumIndex];
  }

  scanAlbumFolder();

  if (albumFiles.empty()) {
    albumIndex = -1;
    return;
  }

  if (current.length()) {
    for (int i = 0; i < (int)albumFiles.size(); ++i) {
      if (albumFiles[i] == current) {
        albumIndex = i;
        return;
      }
    }
  }

  if (albumIndex < 0) albumIndex = 0;
  if (albumIndex >= (int)albumFiles.size()) {
    albumIndex = (int)albumFiles.size() - 1;
  }
}

void saveAlbumPosition() {
  if (albumIndex < 0 ||
      albumIndex >= (int)albumFiles.size()) {
    return;
  }

  albumPrefs.begin("ripleyalbum", false);
  albumPrefs.putString(
    "last",
    albumFiles[albumIndex]
  );
  albumPrefs.end();
  albumHasSavedPhoto = true;
}

int findAlbumFile(const String &path) {
  for (int i = 0; i < (int)albumFiles.size(); ++i) {
    if (albumFiles[i] == path) {
      return i;
    }
  }
  return -1;
}

void chooseInitialAlbumPhoto() {
  albumIndex = -1;
  albumHasSavedPhoto = false;

  if (albumFiles.empty()) return;

  albumPrefs.begin("ripleyalbum", true);
  String saved = albumPrefs.getString("last", "");
  albumPrefs.end();

  if (saved.length()) {
    int found = findAlbumFile(saved);
    if (found >= 0) {
      albumIndex = found;
      albumHasSavedPhoto = true;
      return;
    }
  }

  // First use, or saved file was removed: start with a random image.
  albumIndex = random((int)albumFiles.size());
}

void drawAlbumMessage(const char *line1, const char *line2 = nullptr) {
  M5.Display.fillScreen(gray565(UI_WHITE_GRAY));
  M5.Display.setTextColor(gray565(UI_BLACK_GRAY));
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.drawString(line1, 480, 240);
  if (line2) {
    M5.Display.setTextSize(2);
    M5.Display.drawString(line2, 480, 305);
  }
  M5.Display.setTextDatum(top_left);
  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.display();
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
}


bool readPngSize(const char *path, int &w, int &h) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t b[24];
  if (f.read(b, sizeof(b)) != sizeof(b)) {
    f.close();
    return false;
  }
  f.close();

  static const uint8_t sig[8] =
    {137, 80, 78, 71, 13, 10, 26, 10};

  if (memcmp(b, sig, 8) != 0) return false;

  w = ((int)b[16] << 24) |
      ((int)b[17] << 16) |
      ((int)b[18] << 8)  |
      (int)b[19];

  h = ((int)b[20] << 24) |
      ((int)b[21] << 16) |
      ((int)b[22] << 8)  |
      (int)b[23];

  return w > 0 && h > 0;
}

bool readJpgSize(const char *path, int &w, int &h) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  if (f.read() != 0xFF || f.read() != 0xD8) {
    f.close();
    return false;
  }

  while (f.available()) {
    int c = f.read();
    if (c != 0xFF) continue;

    int marker;
    do {
      marker = f.read();
    } while (marker == 0xFF && f.available());

    if (marker < 0) break;

    // Stand-alone markers without a length field.
    if (marker == 0xD8 || marker == 0xD9 ||
        (marker >= 0xD0 && marker <= 0xD7)) {
      continue;
    }

    int hi = f.read();
    int lo = f.read();
    if (hi < 0 || lo < 0) break;

    int len = (hi << 8) | lo;
    if (len < 2) break;

    const bool sof =
      (marker >= 0xC0 && marker <= 0xC3) ||
      (marker >= 0xC5 && marker <= 0xC7) ||
      (marker >= 0xC9 && marker <= 0xCB) ||
      (marker >= 0xCD && marker <= 0xCF);

    if (sof) {
      f.read();  // precision
      int h1 = f.read();
      int h2 = f.read();
      int w1 = f.read();
      int w2 = f.read();

      h = (h1 << 8) | h2;
      w = (w1 << 8) | w2;
      f.close();
      return w > 0 && h > 0;
    }

    f.seek(f.position() + len - 2);
  }

  f.close();
  return false;
}


float chooseAlbumJpegScale(float fitScale) {
  // JPEG decoders are fastest/cleanest at native power-of-two scales.
  // Common photo sizes map perfectly:
  //   1920x1080 -> 960x540 = 1/2
  //   3840x2160 -> 960x540 = 1/4
  //   7680x4320 -> 960x540 = 1/8
  //
  // Snap only when the requested fit is close enough, otherwise keep
  // the exact aspect-fit scale so unusual aspect ratios still fill well.
  static const float nativeScales[] = {
    1.0f, 0.5f, 0.25f, 0.125f
  };

  for (float nativeScale : nativeScales) {
    const float diff = fabsf(fitScale - nativeScale);
    if (diff <= nativeScale * 0.08f) {
      return nativeScale;
    }
  }

  return fitScale;
}

bool drawCurrentAlbumPhoto() {
  if (albumFiles.empty() ||
      albumIndex < 0 ||
      albumIndex >= (int)albumFiles.size()) {
    if (!sdReady) {
      drawAlbumMessage("SD NOT READY");
    } else if (!SD.exists("/album") && !SD.exists("album")) {
      drawAlbumMessage("ALBUM NOT FOUND", "Create folder: /album");
    } else {
      drawAlbumMessage("NO PHOTOS", "Put JPG/PNG files in /album");
    }
    return false;
  }

  const String &path = albumFiles[albumIndex];
  String lower = path;
  lower.toLowerCase();

  int imageW = 0;
  int imageH = 0;
  bool sizeOk = false;

  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
    sizeOk = readJpgSize(path.c_str(), imageW, imageH);
  } else if (lower.endsWith(".png")) {
    sizeOk = readPngSize(path.c_str(), imageW, imageH);
  }

  if (!sizeOk || imageW <= 0 || imageH <= 0) {
    drawAlbumMessage("CAN'T READ SIZE", albumBaseName(path).c_str());
    return false;
  }

  // AUTO ORIENTATION BY PHOTO SHAPE.
  // Landscape/square -> 960 x 540.
  // Portrait          -> 540 x 960.
  // This is a one-time display rotation only; IMU remains OFF.
  const bool portraitPhoto = (imageH > imageW);

  const int screenW = portraitPhoto ? 540 : 960;
  const int screenH = portraitPhoto ? 960 : 540;

  M5.Display.setRotation(
    portraitPhoto ? 0 : 1
  );

  // Aspect-fit: maximize the whole image without crop/stretch.
  const float sx =
    (float)screenW / (float)imageW;
  const float sy =
    (float)screenH / (float)imageH;

  float scale = (sx < sy) ? sx : sy;

  // Do not upscale small source files.
  if (scale > 1.0f) scale = 1.0f;

  // HIGH-QUALITY path:
  // keep the exact aspect-fit scale instead of snapping JPEG decoding
  // to 1/2, 1/4 or 1/8. This preserves more detail, especially in fur,
  // faces and mid-tone edges. Photos already prepared at 960x540 or
  // 540x960 will use scale 1.0 and are both fastest and sharpest.

  int drawW =
    (int)((float)imageW * scale + 0.5f);
  int drawH =
    (int)((float)imageH * scale + 0.5f);

  if (drawW < 1) drawW = 1;
  if (drawH < 1) drawH = 1;
  if (drawW > screenW) drawW = screenW;
  if (drawH > screenH) drawH = screenH;

  const int drawX =
    (screenW - drawW) / 2;
  const int drawY =
    (screenH - drawH) / 2;

  // POWER v4 / ALBUM HQ:
  // Select the 16-level grayscale path BEFORE decoding/drawing the photo.
  // Previously the photo was rasterized while epd_fastest was active and
  // only switched to epd_quality immediately before display().  That can
  // permanently quantize/dither the framebuffer too aggressively before
  // the quality waveform ever sees it.
  M5.Display.setEpdMode(
    epd_mode_t::epd_quality
  );

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  if (lower.endsWith(".jpg") ||
      lower.endsWith(".jpeg")) {

    M5.Display.drawJpgFile(
      SD,
      path.c_str(),
      drawX,
      drawY,
      screenW - drawX,
      screenH - drawY,
      0,
      0,
      scale,
      0.0f,
      datum_t::top_left
    );

  } else if (lower.endsWith(".png")) {

    M5.Display.drawPngFile(
      SD,
      path.c_str(),
      drawX,
      drawY,
      screenW - drawX,
      screenH - drawY,
      0,
      0,
      scale,
      0.0f,
      datum_t::top_left
    );

  } else {
    drawAlbumMessage("UNSUPPORTED FILE");
    return false;
  }

  // PHOTO QUALITY:
  // Full-quality grayscale waveform for every final still image.
  // No slideshow means this only runs on explicit user interaction.
  // Keep RGB decode path: PaperS3/M5GFX handles this reliably, unlike
  // the experimental 4-bit palette path that produced a gray wash.
  M5.Display.display();

  // Keep QUALITY selected until the physical panel update is finished.
  // This is deliberately album-only; Game keeps its fast refresh path.
  M5.Display.waitDisplay();

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );

  saveAlbumPosition();
  return true;
}

void cleanRefreshCurrentAlbumPhoto() {
  if (albumFiles.empty() ||
      albumIndex < 0 ||
      albumIndex >= (int)albumFiles.size()) {
    return;
  }

  // First make sure rotation matches CURRENT photo so the physical
  // white clean covers the entire active screen in the right geometry.
  int imageW = 0;
  int imageH = 0;
  String lower = albumFiles[albumIndex];
  lower.toLowerCase();

  bool sizeOk = false;
  if (lower.endsWith(".jpg") ||
      lower.endsWith(".jpeg")) {
    sizeOk = readJpgSize(
      albumFiles[albumIndex].c_str(),
      imageW,
      imageH
    );
  } else if (lower.endsWith(".png")) {
    sizeOk = readPngSize(
      albumFiles[albumIndex].c_str(),
      imageW,
      imageH
    );
  }

  if (sizeOk) {
    M5.Display.setRotation(
      (imageH > imageW) ? 0 : 1
    );
  }

  // TRUE CLEAN:
  // 1) full-screen WHITE quality refresh physically clears old ghosting
  // 2) re-decode the SAME photo
  // 3) full photo quality refresh
  deepCleanEpd();
  drawCurrentAlbumPhoto();
}


void drawAlbumPicker() {
  M5.Display.setRotation(1);
  M5.Display.fillScreen(gray565(UI_WHITE_GRAY));
  M5.Display.setTextColor(gray565(UI_BLACK_GRAY));
  M5.Display.setTextDatum(top_left);

  M5.Display.setTextSize(3);
  M5.Display.drawString("ALBUM", 28, 20);

  if (albumFiles.empty()) {
    M5.Display.setTextSize(2);
    M5.Display.drawString("No JPG/PNG files in /album", 28, 100);
  } else {
    const int pages =
      ((int)albumFiles.size() + PHOTO_LIST_ROWS - 1) /
      PHOTO_LIST_ROWS;

    if (albumPickerPage < 0) albumPickerPage = 0;
    if (albumPickerPage >= pages) albumPickerPage = pages - 1;

    const int start = albumPickerPage * PHOTO_LIST_ROWS;

    for (int row = 0; row < PHOTO_LIST_ROWS; ++row) {
      const int idx = start + row;
      if (idx >= (int)albumFiles.size()) break;

      const int y = PHOTO_LIST_TOP + row * PHOTO_LIST_ROW_H;
      M5.Display.drawRect(24, y, 912, PHOTO_LIST_ROW_H - 5,
                          gray565(UI_BLACK_GRAY));

      M5.Display.setTextSize(2);
      String label = albumBaseName(albumFiles[idx]);
      if (label.length() > 42) {
        label = label.substring(0, 39) + "...";
      }
      M5.Display.drawString(label, 42, y + 15);

      if (idx == albumIndex) {
        M5.Display.fillCircle(905, y + 26, 7,
                              gray565(UI_BLACK_GRAY));
      }
    }

    M5.Display.setTextSize(2);
    String pageText =
      String(albumPickerPage + 1) + "/" + String(pages);
    M5.Display.setTextDatum(top_right);
    M5.Display.drawString(pageText, 930, 28);
    M5.Display.setTextDatum(top_left);
  }

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.display();
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
}

void enterPhotoMode() {
  mountSdIfNeeded();

  appMode = MODE_PHOTO;
  saveResumeMode(MODE_PHOTO);

  // No pre-clean here: drawCurrentAlbumPhoto() already performs one
  // full quality refresh. Avoiding a second quality refresh makes
  // Album entry noticeably faster.
  scanAlbumFolder();
  chooseInitialAlbumPhoto();
  drawCurrentAlbumPhoto();

  photoLastTapMs = 0;
}

void returnPhotoToGameMode() {
  mountSdIfNeeded();

  M5.Display.setRotation(0);
  appMode = MODE_GAME;
  saveResumeMode(MODE_GAME);

  forceFullRefresh = true;
  lastStatusMinute = -999;
  lastStatusBattery = -999;

  deepCleanEpd();
  drawFullGame();
}

void showAlbumRelative(int delta) {
  // The SD card may have been updated while the device stayed powered on.
  // Refresh the directory before every navigation gesture so new photos are
  // available immediately.
  rescanAlbumPreserveCurrent();

  if (albumFiles.empty()) return;

  albumIndex += delta;
  if (albumIndex < 0) {
    albumIndex = (int)albumFiles.size() - 1;
  } else if (albumIndex >= (int)albumFiles.size()) {
    albumIndex = 0;
  }

  drawCurrentAlbumPhoto();
}

void openAlbumPicker() {
  M5.Display.setRotation(1);
  rescanAlbumPreserveCurrent();

  if (albumIndex >= 0) {
    albumPickerPage = albumIndex / PHOTO_LIST_ROWS;
  } else {
    albumPickerPage = 0;
  }

  appMode = MODE_PHOTO_PICKER;
  saveResumeMode(MODE_PHOTO);
  drawAlbumPicker();
}

void handleAlbumPickerTap(int x, int y) {
  if (y < PHOTO_LIST_TOP) return;

  const int row = (y - PHOTO_LIST_TOP) / PHOTO_LIST_ROW_H;
  if (row < 0 || row >= PHOTO_LIST_ROWS) return;

  const int idx = albumPickerPage * PHOTO_LIST_ROWS + row;
  if (idx < 0 || idx >= (int)albumFiles.size()) return;

  albumIndex = idx;
  appMode = MODE_PHOTO;
  saveResumeMode(MODE_PHOTO);
  drawCurrentAlbumPhoto();
}

// ============================================================
// AUTO POWER-OFF
// ============================================================

static constexpr uint32_t AUTO_POWEROFF_MS = 2UL * 60UL * 1000UL;
uint32_t lastUserActivityMs = 0;

static inline void markUserActivity() {
  lastUserActivityMs = millis();
}

static void autoPowerOffNow() {
  // Preserve exactly the last visible E-Ink frame.
  M5.Display.waitDisplay();

  // Resume data is continuously checkpointed by the existing logic.
  // True PaperS3 main-power OFF: touch cannot wake it.
  M5.Power.powerOff();

  // Safety fallback; normally powerOff() never returns.
  while (true) {
    delay(1000);
  }
}

static inline void serviceAutoPowerOff(uint32_t nowMs) {
  // Do not cut power while a finger/gesture is active.
  if (touchTracking) {
    return;
  }

  if ((uint32_t)(nowMs - lastUserActivityMs) >= AUTO_POWEROFF_MS) {
    autoPowerOffNow();
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  auto cfg =
    M5.config();

  cfg.internal_imu = false;
  cfg.internal_mic = false;

  // POWER v5 MAX-BATTERY TEST:
  // No clock, no alarm, no RTC wake/timing functionality.
  cfg.internal_rtc = false;

  // No audio functionality in this build.
  cfg.internal_spk = false;

  M5.begin(cfg);

  // Explicitly force both radios OFF.  They are not used anywhere in
  // this firmware and must not remain initialized in the background.
  WiFi.mode(WIFI_OFF);
  btStop();

  M5.Display.setRotation(0);

  M5.Display.setEpdMode(
    epd_mode_t::epd_fastest
  );

  M5.Display.setTextColor(
    gray565(UI_BLACK_GRAY)
  );

  M5.Display.fillScreen(
    gray565(UI_WHITE_GRAY)
  );

  SPI.begin(
    SD_SCK,
    SD_MISO,
    SD_MOSI,
    SD_CS
  );

  mountSdIfNeeded();

  randomSeed(
    (uint32_t)esp_random()
  );

  // Restore the exact active game checkpoint first.  This is independent
  // of the three user SAVE slots, so powering off never overwrites them.
  if (!loadResumeGame()) {
    if (hasGameSlot(1)) {
      selectedSaveSlot = 1;
      loadGame(1);
    } else {
      newGame();
    }
    saveResumeGame();
  }

  // Restore the last user-facing mode after a real main-power cycle.
  // The E-Ink image remains visible while off; on boot we redraw the same
  // state so the application is fully synchronized with the retained panel.
  if (loadResumeMode() == 1) {
    appMode = MODE_PHOTO;
    scanAlbumFolder();
    chooseInitialAlbumPhoto();
    drawCurrentAlbumPhoto();
    photoLastTapMs = 0;
  } else {
    appMode = MODE_GAME;
    forceFullRefresh = true;
    drawFullGame();
  }

  // Begin the inactivity countdown only after boot/resume has completed.
  markUserActivity();
}

// ============================================================
// DISPLAY-SAFE IDLE POWER SAVE
// ============================================================

static inline bool powerGraceActive(uint32_t nowMs) {
  return (int32_t)(powerAwakeUntilMs - nowMs) > 0;
}

static inline void holdPowerAwakeForPanel() {
  powerAwakeUntilMs = millis() + POWER_SLEEP_GRACE_MS;
}

static inline bool eventIdleLightSleep(uint32_t nowMs) {
  // PowerOff Edition:
  // DO NOT enter light/deep sleep and DO NOT arm GT911 touch as a wake source.
  //
  // The device is either:
  //   1) fully ON and immediately responsive, or
  //   2) physically powered OFF by the native PaperS3 side-button
  //      double-click, where touch/CPU/display electronics stop entirely.
  //
  // Keeping this function as a no-op minimizes changes to the mature
  // gesture/render loop.
  (void)nowMs;
  return false;
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  M5.update();

  const uint32_t nowMs = millis();

  serviceAutoPowerOff(nowMs);

  // POWER v5: deliberately no RTC polling, alarm polling,
  // clock maintenance, slideshow timer, Wi-Fi task or Bluetooth task.

  auto t =
    M5.Touch.getDetail();

  if (t.wasPressed()) {

    markUserActivity();

    // A new interaction must use the original mature timing path.
    holdPowerAwakeForPanel();

    touchStartX =
      t.x;

    touchStartY =
      t.y;

    touchStartMs =
      millis();

    touchTracking =
      true;

    // v5 has no CLOCK mode.  Keep the old guard permanently disarmed.
    clockAlbumPressArmed = false;
  }

  if (
    !t.wasReleased() ||
    !touchTracking
  ) {

    // PowerOff Edition stays fully awake while ON. Native side-button
    // double-click performs the real low-current main-power OFF.
    if (!eventIdleLightSleep(nowMs)) {
      // Only used during the short post-refresh / active-touch grace period.
      if (appMode == MODE_PHOTO ||
          appMode == MODE_PHOTO_PICKER) {
        delay(PHOTO_IDLE_DELAY_MS);
      } else {
        delay(GAME_IDLE_DELAY_MS);
      }
    }

    return;
  }

  touchTracking =
    false;
  clockAlbumPressArmed =
    false;

  // The release may trigger a tile redraw, album image, mode transition,
  // settings repaint, etc. Keep the processor awake while that refresh settles.
  holdPowerAwakeForPanel();

  int x =
    t.x;

  int y =
    t.y;

  int dx =
    x - touchStartX;

  int dy =
    y - touchStartY;

  uint32_t heldMs =
    millis() -
    touchStartMs;

  bool stayedStill =
    abs(dx) < 30 &&
    abs(dy) < 30;

  // v5: CLOCK / RTC / ALARM interaction paths removed.

  auto photoCornerLongPress = [&](int sx, int sy, uint32_t held) -> bool {
    if (held < PHOTO_CORNER_REFRESH_MS) return false;

    const int sw = M5.Display.width();
    const int sh = M5.Display.height();
    const int z = PHOTO_CORNER_HOTZONE;

    const bool inCorner =
      (sx < z && sy < z) ||
      (sx >= sw - z && sy < z) ||
      (sx < z && sy >= sh - z) ||
      (sx >= sw - z && sy >= sh - z);

    if (!inCorner) return false;

    // TRUE manual clean: white full refresh, then current photo full refresh.
    // This intentionally takes longer than an ordinary photo change.
    photoLastTapMs = 0;
    cleanRefreshCurrentAlbumPhoto();
    return true;
  };

  // ==========================================================
  // PHOTO PICKER
  // ==========================================================

  if (appMode == MODE_PHOTO_PICKER) {

    if (!stayedStill) {
      if (abs(dx) > abs(dy) &&
          abs(dx) >= PHOTO_SWIPE_THRESHOLD) {

        const int pages =
          ((int)albumFiles.size() + PHOTO_LIST_ROWS - 1) /
          PHOTO_LIST_ROWS;

        if (pages > 1) {
          // Swipe left -> next page, right -> previous page.
          if (dx < 0) albumPickerPage++;
          else albumPickerPage--;

          if (albumPickerPage < 0) albumPickerPage = pages - 1;
          if (albumPickerPage >= pages) albumPickerPage = 0;

          drawAlbumPicker();
        }
      }
      return;
    }

    if (heldMs >= PHOTO_LONG_PRESS_MS) {
      appMode = MODE_PHOTO;
      saveResumeMode(MODE_PHOTO);
      drawCurrentAlbumPhoto();
      return;
    }

    handleAlbumPickerTap(x, y);
    return;
  }

  // ==========================================================
  // PHOTO ALBUM
  // ==========================================================

  if (appMode == MODE_PHOTO) {

    // Horizontal swipe changes photo.  No automatic slideshow exists.
    if (!stayedStill &&
        abs(dx) > abs(dy) &&
        abs(dx) >= PHOTO_SWIPE_THRESHOLD) {

      // Swipe left -> next; swipe right -> previous.
      showAlbumRelative(dx < 0 ? +1 : -1);
      photoLastTapMs = 0;
      return;
    }

    // Long-press any corner: manually clean-refresh CURRENT photo.
    // This has priority over the normal PHOTO long-press action.
    if (stayedStill &&
        photoCornerLongPress(
          touchStartX,
          touchStartY,
          heldMs
        )) {
      return;
    }

    // Long press elsewhere completes the cycle: PHOTO -> GAME.
    if (stayedStill &&
        heldMs >= PHOTO_LONG_PRESS_MS) {

      photoLastTapMs = 0;
      returnPhotoToGameMode();
      return;
    }

    // Double tap opens the /album file picker.
    if (stayedStill) {
      const uint32_t tapNow = millis();

      if (photoLastTapMs != 0 &&
          (uint32_t)(tapNow - photoLastTapMs) <=
            PHOTO_DOUBLE_TAP_MS) {

        photoLastTapMs = 0;
        openAlbumPicker();
        return;
      }

      photoLastTapMs = tapNow;
    }

    return;
  }

  // v5: no CLOCK mode.

  // ==========================================================
  // HIDDEN DEVELOPER MILESTONE TESTER
  // ==========================================================

  const bool startedOnLoadIcon = pointInRect(
    touchStartX, touchStartY,
    LOAD_ICON_X, SIDE_ICON_Y, SIDE_ICON_SIZE, SIDE_ICON_SIZE
  );

  const bool startedOnSaveIcon = pointInRect(
    touchStartX, touchStartY,
    SAVE_ICON_X, SIDE_ICON_Y, SIDE_ICON_SIZE, SIDE_ICON_SIZE
  );

  // Hold LOAD triangle for 10 seconds to toggle developer test mode.
  if (stayedStill && heldMs >= DEV_LOAD_LONG_PRESS_MS && startedOnLoadIcon) {
    if (developerTestMode) exitDeveloperTestMode();
    else enterDeveloperTestMode();
    return;
  }

  if (developerTestMode) {
    // Short tap LOAD triangle -> next milestone/background.
    if (stayedStill && heldMs < DEV_LOAD_LONG_PRESS_MS && startedOnLoadIcon) {
      showDeveloperTestLevel((developerTestLevel + 1) & 7);
      return;
    }

    // Short tap SAVE triangle -> previous milestone/background.
    if (stayedStill && startedOnSaveIcon) {
      showDeveloperTestLevel((developerTestLevel + 7) & 7);
      return;
    }

    // Ignore all other GAME gestures while testing so the real game
    // cannot accidentally be overwritten or moved.
    return;
  }

  // ==========================================================
  // GAME MODE LONG PRESSES
  // ==========================================================

  // v5: Long-press 2048 title -> Album directly.
  // This replaces the old GAME -> CLOCK -> ALBUM route.
  if (
    stayedStill &&
    heldMs >= TITLE_LONG_PRESS_MS &&
    pointInRect(
      touchStartX,
      touchStartY,
      TITLE_TOUCH_X,
      TITLE_TOUCH_Y,
      TITLE_TOUCH_W,
      TITLE_TOUCH_H
    )
  ) {

    enterPhotoMode();
    return;
  }

  // Long-press level image -> manual quality full refresh.
  if (
    stayedStill &&
    heldMs >= BG_REFRESH_LONG_PRESS_MS &&
    pointInRect(
      touchStartX,
      touchStartY,
      BACKGROUND_X,
      BACKGROUND_Y,
      BACKGROUND_W,
      BACKGROUND_H
    )
  ) {

    forceFullRefresh =
      true;

    // Manual refresh is intentionally stronger than the normal
    // periodic refresh: clear the physical E-Ink first, then
    // redraw the whole game.
    deepCleanEpd();

    drawFullGame();

    return;
  }

  // ==========================================================
  // GAME MODE TAP BUTTONS
  // ==========================================================

  if (
    abs(dx) < 35 &&
    abs(dy) < 35
  ) {

    // NEW GAME — right-side circular-arrow icon.
    if (
      x >= NEW_X &&
      x <= NEW_X + NEW_W &&
      y >= NEW_Y &&
      y <= NEW_Y + NEW_H
    ) {

      newGame();
      saveResumeGame();
      saveResumeMode(MODE_GAME);
      drawFullGame();
      return;
    }

    // SAVE slots 1 / 2 / 3 — left side.
    if (
      x >= SAVE_SLOT_X &&
      x < SAVE_SLOT_X + SLOT_W
    ) {

      int slot = 0;

      if (
        y >= SLOT_Y1 &&
        y < SLOT_Y1 + SLOT_H
      ) {
        slot = 1;
      } else if (
        y >= SLOT_Y2 &&
        y < SLOT_Y2 + SLOT_H
      ) {
        slot = 2;
      } else if (
        y >= SLOT_Y3 &&
        y < SLOT_Y3 + SLOT_H
      ) {
        slot = 3;
      }

      if (slot != 0) {

        selectedSaveSlot =
          slot;

        saveGame(
          slot
        );

        forceFullRefresh =
          true;

        drawFullGame();
        return;
      }
    }

    // LOAD slots 1 / 2 / 3 — right side.
    if (
      x >= LOAD_SLOT_X &&
      x < LOAD_SLOT_X + SLOT_W
    ) {

      int slot = 0;

      if (
        y >= SLOT_Y1 &&
        y < SLOT_Y1 + SLOT_H
      ) {
        slot = 1;
      } else if (
        y >= SLOT_Y2 &&
        y < SLOT_Y2 + SLOT_H
      ) {
        slot = 2;
      } else if (
        y >= SLOT_Y3 &&
        y < SLOT_Y3 + SLOT_H
      ) {
        slot = 3;
      }

      if (slot != 0) {

        loadGame(
          slot
        );

        saveResumeGame();
        saveResumeMode(MODE_GAME);

        forceFullRefresh =
          true;

        drawFullGame();
        return;
      }
    }
  }

  // ==========================================================
  // IGNORE TINY MOVEMENT
  // ==========================================================

  if (
    abs(dx) < 45 &&
    abs(dy) < 45
  ) {

    return;
  }

  // ==========================================================
  // GAME OVER
  // ==========================================================

  if (gameOver) {

    newGame();
    saveResumeGame();
    saveResumeMode(MODE_GAME);
    drawFullGame();

    return;
  }

  // ==========================================================
  // SAVE OLD STATE
  // ==========================================================

  copyBoard(
    board,
    oldBoard
  );

  oldScore =
    score;

  oldBestScore =
    bestScore;

  oldGameOver =
    gameOver;

  oldBackgroundLevel =
    backgroundLevel;

  // ==========================================================
  // DIRECTION
  // ==========================================================

  int dir;

  if (
    abs(dx) >
    abs(dy)
  ) {

    dir =
      dx > 0
        ? 1
        : 3;

  } else {

    dir =
      dy > 0
        ? 2
        : 0;
  }

  // ==========================================================
  // MOVE
  // ==========================================================

  if (moveGame(dir)) {

    addRandomTile();

    if (score > bestScore) {
      bestScore = score;
    }

    updateBackgroundLevel();

    gameOver =
      isGameOver();

    moveCount++;

    // Checkpoint BEFORE any possible hardware power-off.
    saveResumeGame();
    saveResumeMode(MODE_GAME);

    if (
      FULL_REFRESH_INTERVAL > 0 &&
      moveCount >=
      FULL_REFRESH_INTERVAL
    ) {

      moveCount = 0;
      forceFullRefresh = true;
    }

    drawGame();
  }

  delay(GAME_IDLE_DELAY_MS);
}
