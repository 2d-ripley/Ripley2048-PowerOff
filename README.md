# Ripley2048 PaperS3 — PowerOff Edition

## Power behavior

PaperS3 native hardware behavior is preserved:

- Single-click side button: power ON.
- Double-click side button: main power OFF.
- Long press with USB connected: download mode (rear red LED flashes).

This firmware intentionally does **not** use touch-wake light sleep. While the device is ON, it is fully responsive. When the PaperS3 hardware powers OFF, E-Ink keeps the last image while CPU/touch/display electronics stop.

## Resume behavior

Because the hardware power controller can remove power without an application shutdown callback:

- Game state is checkpointed to ESP32 NVS after every valid move, New Game, and Load.
- The three manual SD save slots remain separate and are not overwritten by resume.
- Last mode (Game/Album) is stored.
- Album's last photo continues to use the existing `ripleyalbum` Preferences record.

On next single-click power-on, the firmware cold-boots directly back to the last Game or Album state.

## Build

GitHub Actions builds both:
- `firmware.bin`
- `Ripley2048_factory.bin` (merged image, flash at offset `0x0000`)

## Flash

### Recommended
Mac + Chrome/Edge + ESP Web Tools installer.

1. Connect PaperS3 by USB.
2. Long-press Power until rear LED flashes red.
3. Open the GitHub Pages installer.
4. Connect and Install.

### Recovery
M5Burner on macOS can be used as a fallback.

## GitHub Pages

The `web/` directory contains the ESP Web Tools installer and manifest.
After the Actions build, copy/deploy the generated `web/firmware/Ripley2048_factory.bin`
with the rest of `web/`.

If using GitHub Pages through Actions, add a deploy-pages job or publish the `web`
artifact according to the repository's Pages settings.
