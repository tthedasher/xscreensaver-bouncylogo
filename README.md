# Bouncy Logo - XScreensaver Hack

A DVD-style bouncing logo screensaver for Linux/Unix systems using X11 and Imlib2. The logo bounces around the screen, changing colors on each bounce, with a special celebration animation when it hits a corner.

![Bouncy Logo in action](Linux.png)

## Features

- **Smooth animation**: 60 FPS by default with double-buffering
- **Color cycling**: Random color changes on boundary bounces
- **Corner detection**: Special celebration effect when hitting corners
- **Configurable**: Adjustable speed, FPS, and scaling
- **XScreensaver integration**: Seamless integration with standard XScreensaver

## Requirements

- Linux/Unix system with X11
- GCC or compatible C compiler
- Development libraries:
  - `libimlib2-dev` (or equivalent for your distro)
  - `libx11-dev`
  - `libxext-dev`
  - `pkg-config`

### Installation of Dependencies

**Debian/Ubuntu:**
```bash
sudo apt install build-essential libimlib2-dev libx11-dev libxext-dev pkg-config
