from pathlib import Path

MAIN = Path('src/main.cpp')
s = MAIN.read_text()

needle = '''    touchTracking =\n      true;\n\n    // v5 has no CLOCK mode.  Keep the old guard permanently disarmed.\n    clockAlbumPressArmed = false;\n'''

insert = '''    touchTracking =\n      true;\n\n    // v1.3.2 reliability fix: SAVE/LOAD slot buttons live at the extreme\n    // left/right edges of the GT911 panel. A press can be detected while\n    // the corresponding release event is occasionally lost, so waiting for\n    // wasReleased() makes these buttons appear intermittent.\n    // Handle only the six slot buttons immediately on PRESS. They are outside\n    // the swipe board and do not overlap the 10-second DEV triangle gesture.\n    if (appMode == MODE_GAME && !developerTestMode) {\n      int pressedSlot = 0;\n      if (t.y >= SLOT_Y1 && t.y < SLOT_Y1 + SLOT_H) pressedSlot = 1;\n      else if (t.y >= SLOT_Y2 && t.y < SLOT_Y2 + SLOT_H) pressedSlot = 2;\n      else if (t.y >= SLOT_Y3 && t.y < SLOT_Y3 + SLOT_H) pressedSlot = 3;\n\n      if (pressedSlot != 0 &&\n          t.x >= SAVE_SLOT_X && t.x < SAVE_SLOT_X + SLOT_W) {\n        selectedSaveSlot = pressedSlot;\n        saveGame(pressedSlot);\n        forceFullRefresh = true;\n        drawFullGame();\n        touchTracking = false;\n        return;\n      }\n\n      if (pressedSlot != 0 &&\n          t.x >= LOAD_SLOT_X && t.x < LOAD_SLOT_X + SLOT_W) {\n        loadGame(pressedSlot);\n        saveResumeGame();\n        saveResumeMode(MODE_GAME);\n        forceFullRefresh = true;\n        drawFullGame();\n        touchTracking = false;\n        return;\n      }\n    }\n\n    // v5 has no CLOCK mode.  Keep the old guard permanently disarmed.\n    clockAlbumPressArmed = false;\n'''

if s.count(needle) != 1:
    raise SystemExit(f'press insertion point: expected 1 match, found {s.count(needle)}')

s = s.replace(needle, insert, 1)
MAIN.write_text(s)
print('v1.3.2 press-driven SAVE/LOAD fix applied')
