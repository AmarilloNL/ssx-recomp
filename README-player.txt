SSX (2012) - native PC version
==============================

You need your own copy of the Xbox 360 game (the disc .iso, or the files
extracted from it). No game data is included.

Starting
  Double-click ssx-recomp.exe in your SSX folder (or use the desktop
  shortcut) and click Play SSX. It starts the game, ssx.exe in this folder,
  which you can also start directly. If the game doesn't know your game files,
  it asks: pick the .iso, or default.xex inside the extracted disc folder. It
  remembers the choice.
  (You can also put the .iso, or a folder named "game", next to ssx.exe.)
  To update SSX: click Check for updates in ssx-recomp.exe (the only thing
  that goes online). To add DLC or the title update: Add DLC / rebuild.

Settings
  F1   Settings menu (F1 or Esc closes it). On a controller: Back + Start
       opens it, d-pad/stick + A to change, LB/RB switch tabs, B closes.
       Changes are saved to ssx.toml
       next to ssx.exe; some need a restart (the menu offers "Restart now").
       Quit game (bottom right) closes SSX, also from a controller.
       Display   fullscreen, frame rate (30 to 300, or match your monitor), VSync,
                 FPS counter, the boost meter in freeride
       Graphics  resolution scale, upscaling (AMD FSR 1 by default), sharpness,
                 anti-aliasing, texture filtering
       Audio     volume
       Controls  keyboard, vibration, deadzone, key bindings
       Profile   your name and picture, the badges that need EA's servers
       Snowflakes  how many snowflakes you have on every drop
       Cheats    infinite boost; Tricky or Super Tricky the whole ride, or no Tricky;
                 infinite credits (your own credits stay as they are)

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
  2. Copy the package FILES (not the folder) into userdata\dlc (next to ssx.exe).
  3. Start the game: new packages are installed automatically, once.
  Check: Mt. Eddie shows on the world map, and logs\ssx_*.log has a line
  "DLC: installed ...". The files in the dlc folder can be deleted afterwards.

Title update (optional, not included - you need it from your own Xbox 360)
  SSX's last update (~9 MB) adds the gear for levels 11 to 15. On the
  console it is
  in Content\0000000000000000\4541096D\000B0000\ (if there are two files, use
  the larger one).
  In ssx-recomp.exe click Add DLC / rebuild, then "Add files...": the game is
  rebuilt with it (the "update" folder next to ssx.exe). Saves work with and
  without it.

Levels 11 to 15 (needs the title update)
  EA raised the level cap of 10 through its servers, which the port can't
  reach (they go through Xbox Live). With the title update installed the
  port raises it itself, nothing to set up:
  a level 10 rider is offered access to level 11 on the rider screen (it
  costs credits), and so on up to level 15.

Your own music
  Put your music in userdata\music (next to ssx.exe), one folder per playlist (MP3, M4A,
  WMA, FLAC, WAV). In the game: Back on the main screen > Music >
  Personalize Your Music, pick "Playlist" and one of your folders for race,
  trick, survive or menu music. The game remixes it as you ride.

Saves, settings, DLC, music and shader cache all live in this folder (the
userdata folder and ssx.toml here): nothing is kept anywhere else. The saves
themselves are in userdata\B13EBABEBABEBABE\4541096D\00000001 (back up that
folder, userdata\tuning, userdata\achievements and ssx.toml).
Window: fullscreen by default (F1 > Display). Started from RecompLauncher, SSX
opens fullscreen in its Big Picture mode and in a window on the desktop,
unless you set Fullscreen yourself (ssx_window in ssx.toml: auto, fullscreen
or windowed).
If a setting stops the game from starting, delete ssx.toml next to ssx.exe
to go back to the defaults (you will be asked for the game once more).
