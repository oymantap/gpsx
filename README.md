# Neon Runner PSX

A tiny PlayStation 1 homebrew game built with PSn00bSDK.

## Game
Move the blue square with the D-pad and avoid the falling red blocks.
Survive as long as possible. Press START to begin/restart.

## Build
Push this repository to GitHub. GitHub Actions downloads the official PSn00bSDK v0.24 Linux package, configures CMake, builds the PS1 executable, and creates a BIN/CUE CD image.

The resulting artifacts are:
- `neon_runner.bin`
- `neon_runner.cue`
- `neon_runner.exe`

The BIN/CUE can be opened in a PS1 emulator such as DuckStation.

PSn00bSDK is an open-source PS1 SDK by Lameguy64.
