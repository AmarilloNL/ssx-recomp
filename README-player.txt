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

DLC (optional, not included - you need it from your own Xbox 360)
  SSX's DLC packages are files with long hexadecimal names and no extension.
  On the console they are in Content\0000000000000000\4541096D\00000002\;
  copy them to your PC with an Xbox 360 file tool (FATXplorer, Velocity, Horizon).
  1. Close the game.
  2. Copy the package FILES (not the folder) into Documents\ssx\dlc.
  3. Start the game: new packages are installed automatically, once.
  Check: Mt. Eddie shows on the world map, and logs\ssx_*.log has a line
  "DLC: installed ...". The files in the dlc folder can be deleted afterwards.

Title update (optional, not included - you need it from your own Xbox 360)
  SSX's last update (~9 MB) adds the gear for levels 11 to 15. On the
  console it is
  in Content\0000000000000000\4541096D\000B0000\ (if there are two files, use
  the larger one).
  Run SSX-Installer.exe again and add it with "Add files...": the game is
  rebuilt with it (the "update" folder next to ssx.exe). Saves work with and
  without it.

Levels 11 to 15 (needs the title update)
  EA raised the level cap of 10 through its servers, which the port can't
  reach (they go through Xbox Live). With the title update installed the
  port raises it itself, nothing to set up:
  a level 10 rider is offered access to level 11 on the rider screen (it
  costs credits), and so on up to level 15.

Saves, DLC and shader cache live in Documents\ssx.
If a setting stops the game from starting, delete ssx.toml next to ssx.exe
to go back to the defaults (you will be asked for the game once more).
