@echo off
rem Launches the SSX PC port. Log goes to ssx.log next to this file.
cd /d "%~dp0out\build\ssx"
ssx.exe --game_data_root="%~dp0game-dump\files" --log_file="%~dp0ssx.log" %*
