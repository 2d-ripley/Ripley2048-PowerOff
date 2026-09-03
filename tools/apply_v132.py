from pathlib import Path

MAIN = Path('src/main.cpp')
MANIFEST = Path('web/manifest.json')

s = MAIN.read_text()


def replace_once(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f'{label}: expected exactly 1 match, found {n}')
    s = s.replace(old, new, 1)

# Version label only; do not disturb proven rendering/game/power logic.
s = s.replace('PowerOff Edition v1.3.1', 'PowerOff Edition v1.3.2', 1)

# Album picker delete UI constants.
replace_once(
    'static constexpr uint32_t PHOTO_CORNER_REFRESH_MS = 1000;\n',
    'static constexpr uint32_t PHOTO_CORNER_REFRESH_MS = 1000;\n\n'
    '// v1.3.2: long-press a picker row to request deletion.\n'
    'static constexpr uint32_t PHOTO_DELETE_LONG_PRESS_MS = 1500;\n'
    'static constexpr int PHOTO_DELETE_YES_X = 220;\n'
    'static constexpr int PHOTO_DELETE_YES_Y = 390;\n'
    'static constexpr int PHOTO_DELETE_BUTTON_W = 220;\n'
    'static constexpr int PHOTO_DELETE_BUTTON_H = 72;\n'
    'static constexpr int PHOTO_DELETE_CANCEL_X = 520;\n'
    'static constexpr int PHOTO_DELETE_CANCEL_Y = 390;\n',
    'photo delete constants')

# Add confirmation state to Album mode family.
replace_once(
    '  MODE_PHOTO,\n  MODE_PHOTO_PICKER,\n  MODE_ALARM_RINGING\n',
    '  MODE_PHOTO,\n  MODE_PHOTO_PICKER,\n  MODE_PHOTO_DELETE_CONFIRM,\n  MODE_ALARM_RINGING\n',
    'delete app mode')

replace_once(
    'uint32_t photoLastTapMs = 0;\n',
    'uint32_t photoLastTapMs = 0;\n\n'
    '// v1.3.2 album delete confirmation state.\n'
    'int albumDeleteIndex = -1;\n'
    'String albumDeletePath = "";\n'
    'bool albumDeleteFailed = false;\n',
    'delete state')

replace_once(
    '  uint8_t m = (mode == MODE_PHOTO || mode == MODE_PHOTO_PICKER) ? 1 : 0;\n',
    '  uint8_t m = (mode == MODE_PHOTO || mode == MODE_PHOTO_PICKER ||\n'
    '               mode == MODE_PHOTO_DELETE_CONFIRM) ? 1 : 0;\n',
    'resume mode')

# Visible DEV badge. It is deliberately drawn only on full GAME redraws,
# which are already used by every developer milestone change and exit.
replace_once(
    'static inline bool eventIdleLightSleep(uint32_t nowMs);\n',
    'static inline bool eventIdleLightSleep(uint32_t nowMs);\n\n'
    'static void drawDeveloperBadge() {\n'
    '  if (!developerTestMode) return;\n'
    '  M5.Display.setTextDatum(top_left);\n'
    '  M5.Display.setTextColor(gray565(UI_BLACK_GRAY));\n'
    '  M5.Display.setTextSize(2);\n'
    '  M5.Display.drawString("DEV", 470, 28);\n'
    '}\n',
    'developer badge helper')

replace_once(
    '  drawGameOver();\n  drawStatusBar();\n\n  // Commit the already-quality-composed framebuffer in one full refresh.\n',
    '  drawGameOver();\n  drawStatusBar();\n  drawDeveloperBadge();\n\n'
    '  // Commit the already-quality-composed framebuffer in one full refresh.\n',
    'developer badge draw')

# Album delete confirmation helpers. Existing picker rendering and photo HQ
# rendering remain untouched.
replace_once(
    'void enterPhotoMode() {\n',
    '''static int albumPickerIndexAt(int x, int y) {
  if (x < 24 || x >= 936 || y < PHOTO_LIST_TOP) return -1;
  const int row = (y - PHOTO_LIST_TOP) / PHOTO_LIST_ROW_H;
  if (row < 0 || row >= PHOTO_LIST_ROWS) return -1;
  const int rowTop = PHOTO_LIST_TOP + row * PHOTO_LIST_ROW_H;
  if (y >= rowTop + PHOTO_LIST_ROW_H - 5) return -1;
  const int idx = albumPickerPage * PHOTO_LIST_ROWS + row;
  if (idx < 0 || idx >= (int)albumFiles.size()) return -1;
  return idx;
}

void drawAlbumDeleteConfirm() {
  M5.Display.setRotation(1);
  M5.Display.fillScreen(gray565(UI_WHITE_GRAY));
  M5.Display.setTextColor(gray565(UI_BLACK_GRAY));
  M5.Display.setTextDatum(top_left);

  M5.Display.setTextSize(4);
  M5.Display.drawString(albumDeleteFailed ? "DELETE FAILED" : "DELETE PHOTO?", 70, 75);

  M5.Display.setTextSize(2);
  String label = albumBaseName(albumDeletePath);
  if (label.length() > 64) label = label.substring(0, 61) + "...";
  M5.Display.drawString(label, 70, 170);

  if (!albumDeleteFailed) {
    M5.Display.drawString("This permanently removes the file from /album.", 70, 245);
    drawButton(PHOTO_DELETE_YES_X, PHOTO_DELETE_YES_Y,
               PHOTO_DELETE_BUTTON_W, PHOTO_DELETE_BUTTON_H, "DELETE");
  }

  drawButton(PHOTO_DELETE_CANCEL_X, PHOTO_DELETE_CANCEL_Y,
             PHOTO_DELETE_BUTTON_W, PHOTO_DELETE_BUTTON_H,
             albumDeleteFailed ? "BACK" : "CANCEL");

  M5.Display.setEpdMode(epd_mode_t::epd_quality);
  M5.Display.display();
  M5.Display.setEpdMode(epd_mode_t::epd_fastest);
}

void enterAlbumDeleteConfirm(int idx) {
  if (idx < 0 || idx >= (int)albumFiles.size()) return;
  albumDeleteIndex = idx;
  albumDeletePath = albumFiles[idx];
  albumDeleteFailed = false;
  appMode = MODE_PHOTO_DELETE_CONFIRM;
  saveResumeMode(MODE_PHOTO);
  drawAlbumDeleteConfirm();
}

void cancelAlbumDelete() {
  albumDeleteIndex = -1;
  albumDeletePath = "";
  albumDeleteFailed = false;
  appMode = MODE_PHOTO_PICKER;
  drawAlbumPicker();
}

void confirmAlbumDelete() {
  if (albumDeleteIndex < 0 ||
      albumDeleteIndex >= (int)albumFiles.size() ||
      albumDeletePath.length() == 0) {
    cancelAlbumDelete();
    return;
  }

  const int deletedIndex = albumDeleteIndex;
  String currentPath = "";
  if (albumIndex >= 0 && albumIndex < (int)albumFiles.size()) {
    currentPath = albumFiles[albumIndex];
  }

  mountSdIfNeeded();
  if (!sdReady || !SD.remove(albumDeletePath.c_str())) {
    albumDeleteFailed = true;
    drawAlbumDeleteConfirm();
    return;
  }

  const bool deletedCurrent = (currentPath == albumDeletePath);
  scanAlbumFolder();

  if (albumFiles.empty()) {
    albumIndex = -1;
    albumPickerPage = 0;
  } else if (!deletedCurrent && currentPath.length()) {
    int found = -1;
    for (int i = 0; i < (int)albumFiles.size(); ++i) {
      if (albumFiles[i] == currentPath) {
        found = i;
        break;
      }
    }
    albumIndex = (found >= 0)
      ? found
      : min(deletedIndex, (int)albumFiles.size() - 1);
    albumPickerPage = min(deletedIndex, (int)albumFiles.size() - 1) /
                      PHOTO_LIST_ROWS;
  } else {
    albumIndex = min(deletedIndex, (int)albumFiles.size() - 1);
    albumPickerPage = albumIndex / PHOTO_LIST_ROWS;
  }

  if (albumIndex >= 0) saveAlbumPosition();

  albumDeleteIndex = -1;
  albumDeletePath = "";
  albumDeleteFailed = false;
  appMode = MODE_PHOTO_PICKER;
  drawAlbumPicker();
}

void handleAlbumDeleteConfirmTap(int x, int y) {
  if (albumDeleteFailed) {
    if (pointInRect(x, y,
                    PHOTO_DELETE_CANCEL_X, PHOTO_DELETE_CANCEL_Y,
                    PHOTO_DELETE_BUTTON_W, PHOTO_DELETE_BUTTON_H)) {
      cancelAlbumDelete();
    }
    return;
  }

  if (pointInRect(x, y,
                  PHOTO_DELETE_YES_X, PHOTO_DELETE_YES_Y,
                  PHOTO_DELETE_BUTTON_W, PHOTO_DELETE_BUTTON_H)) {
    confirmAlbumDelete();
    return;
  }

  if (pointInRect(x, y,
                  PHOTO_DELETE_CANCEL_X, PHOTO_DELETE_CANCEL_Y,
                  PHOTO_DELETE_BUTTON_W, PHOTO_DELETE_BUTTON_H)) {
    cancelAlbumDelete();
  }
}

void enterPhotoMode() {
''',
    'album delete helpers')

# Confirmation screen gets first chance to consume taps.
replace_once(
    '  // ==========================================================\n  // PHOTO PICKER\n  // ==========================================================\n\n  if (appMode == MODE_PHOTO_PICKER) {\n',
    '  // ==========================================================\n  // PHOTO DELETE CONFIRMATION\n  // ==========================================================\n\n'
    '  if (appMode == MODE_PHOTO_DELETE_CONFIRM) {\n'
    '    if (stayedStill) handleAlbumDeleteConfirmTap(x, y);\n'
    '    return;\n'
    '  }\n\n'
    '  // ==========================================================\n  // PHOTO PICKER\n  // ==========================================================\n\n'
    '  if (appMode == MODE_PHOTO_PICKER) {\n',
    'confirm mode loop')

# Long press a real row -> confirm delete. Long press outside a row keeps the
# old picker->photo behavior.
replace_once(
    '    if (heldMs >= PHOTO_LONG_PRESS_MS) {\n'
    '      appMode = MODE_PHOTO;\n'
    '      saveResumeMode(MODE_PHOTO);\n'
    '      drawCurrentAlbumPhoto();\n'
    '      return;\n'
    '    }\n\n'
    '    handleAlbumPickerTap(x, y);\n',
    '    if (heldMs >= PHOTO_DELETE_LONG_PRESS_MS) {\n'
    '      const int idx = albumPickerIndexAt(touchStartX, touchStartY);\n'
    '      if (idx >= 0) {\n'
    '        enterAlbumDeleteConfirm(idx);\n'
    '        return;\n'
    '      }\n'
    '    }\n\n'
    '    if (heldMs >= PHOTO_LONG_PRESS_MS) {\n'
    '      appMode = MODE_PHOTO;\n'
    '      saveResumeMode(MODE_PHOTO);\n'
    '      drawCurrentAlbumPhoto();\n'
    '      return;\n'
    '    }\n\n'
    '    handleAlbumPickerTap(x, y);\n',
    'picker long press')

MAIN.write_text(s)

if MANIFEST.exists():
    m = MANIFEST.read_text()
    m = m.replace('"version": "1.3.1"', '"version": "1.3.2"')
    MANIFEST.write_text(m)

print('v1.3.2 patch applied')
