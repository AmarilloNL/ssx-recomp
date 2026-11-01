SSX (2012) - native PC version
==============================

You need your own copy of the Xbox 360 game (the disc .iso, or the files
extracted from it). No game data is included.

Starting
  Double-click ssx.exe. The first time, it asks for your game: pick the .iso,
  or default.xex inside the extracted disc folder. It remembers the choice.
  (You can also put the .iso, or a folder named "game", next to ssx.exe.)

Settings
  F1   Settings menu (F1 or Esc closes it). On a controller: Back + Start
       opens it, d-pad/stick + A to change, LB/RB switch tabs, B closes.
       Changes are saved to ssx.toml
       next to ssx.exe; some need a restart (the menu offers "Restart now").
       Display   fullscreen, frame rate (30 to 300, or match your monitor), FPS counter
       Graphics  resolution scale, upscaling (AMD FSR 1 by default), sharpness,
                 anti-aliasing, texture filtering
       Audio     volume
       Controls  keyboard, vibration, deadzone, key bindings

  Tip: 1x + FSR runs much faster than 2x and still looks sharp.
       2x is the sharpest but limited to around 60 fps by the GPU emulation.

Keyboard (default)
  WASD steer, Space jump (A), arrow keys = trick stick, Q/E triggers,
  1/3 bumpers, Enter start, Tab back. Change them in F1 > Controls.

DLC
  Copy the DLC package files (the ones inside 4541096D\00000002\) into
  Documents\ssx\dlc. They are installed the next time the game starts.

Saves, DLC and shader cache live in Documents\ssx.
If a setting stops the game from starting, delete ssx.toml next to ssx.exe
to go back to the defaults (you will be asked for the game once more).
