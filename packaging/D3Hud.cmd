@echo off
rem Starts the D3Hud overlay on top of Diablo III and lets it launch the reader.
rem Start the game first (windowed or windowed fullscreen), then run this.
rem Options: edit the line below, or run D3Hud.Overlay.exe --help.
cd /d "%~dp0"
start "" "%~dp0D3Hud.Overlay.exe" --target "Diablo III" --start-reader
