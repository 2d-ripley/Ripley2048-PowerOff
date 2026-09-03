from pathlib import Path

MAIN = Path('src/main.cpp')
s = MAIN.read_text()

old = '''    // SAVE slots 1 / 2 / 3 — left side.\n    if (\n      x >= SAVE_SLOT_X &&\n      x < SAVE_SLOT_X + SLOT_W\n    ) {\n\n      int slot = 0;\n\n      if (\n        y >= SLOT_Y1 &&\n        y < SLOT_Y1 + SLOT_H\n      ) {\n        slot = 1;\n      } else if (\n        y >= SLOT_Y2 &&\n        y < SLOT_Y2 + SLOT_H\n      ) {\n        slot = 2;\n      } else if (\n        y >= SLOT_Y3 &&\n        y < SLOT_Y3 + SLOT_H\n      ) {\n        slot = 3;\n      }\n'''

new = '''    // SAVE slots 1 / 2 / 3 — left side.\n    // Use touch-start coordinates for taps. GT911 release coordinates can\n    // drift a few pixels at the extreme screen edge, making these narrow\n    // controls appear intermittent even though the press was valid.\n    if (\n      touchStartX >= SAVE_SLOT_X &&\n      touchStartX < SAVE_SLOT_X + SLOT_W\n    ) {\n\n      int slot = 0;\n\n      if (\n        touchStartY >= SLOT_Y1 &&\n        touchStartY < SLOT_Y1 + SLOT_H\n      ) {\n        slot = 1;\n      } else if (\n        touchStartY >= SLOT_Y2 &&\n        touchStartY < SLOT_Y2 + SLOT_H\n      ) {\n        slot = 2;\n      } else if (\n        touchStartY >= SLOT_Y3 &&\n        touchStartY < SLOT_Y3 + SLOT_H\n      ) {\n        slot = 3;\n      }\n'''

if s.count(old) != 1:
    raise SystemExit(f'SAVE block: expected 1 match, found {s.count(old)}')
s = s.replace(old, new, 1)

old = '''    // LOAD slots 1 / 2 / 3 — right side.\n    if (\n      x >= LOAD_SLOT_X &&\n      x < LOAD_SLOT_X + SLOT_W\n    ) {\n\n      int slot = 0;\n\n      if (\n        y >= SLOT_Y1 &&\n        y < SLOT_Y1 + SLOT_H\n      ) {\n        slot = 1;\n      } else if (\n        y >= SLOT_Y2 &&\n        y < SLOT_Y2 + SLOT_H\n      ) {\n        slot = 2;\n      } else if (\n        y >= SLOT_Y3 &&\n        y < SLOT_Y3 + SLOT_H\n      ) {\n        slot = 3;\n      }\n'''

new = '''    // LOAD slots 1 / 2 / 3 — right side.\n    // Same touch-start rule as SAVE; especially important at x=483..539.\n    if (\n      touchStartX >= LOAD_SLOT_X &&\n      touchStartX < LOAD_SLOT_X + SLOT_W\n    ) {\n\n      int slot = 0;\n\n      if (\n        touchStartY >= SLOT_Y1 &&\n        touchStartY < SLOT_Y1 + SLOT_H\n      ) {\n        slot = 1;\n      } else if (\n        touchStartY >= SLOT_Y2 &&\n        touchStartY < SLOT_Y2 + SLOT_H\n      ) {\n        slot = 2;\n      } else if (\n        touchStartY >= SLOT_Y3 &&\n        touchStartY < SLOT_Y3 + SLOT_H\n      ) {\n        slot = 3;\n      }\n'''

if s.count(old) != 1:
    raise SystemExit(f'LOAD block: expected 1 match, found {s.count(old)}')
s = s.replace(old, new, 1)

MAIN.write_text(s)
print('v1.3.2 SAVE/LOAD touch-start fix applied')
