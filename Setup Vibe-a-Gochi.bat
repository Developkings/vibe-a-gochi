@echo off
rem Double-click: sets up this PC for the Vibe-a-Gochi board (see companion\setup.py).
rem   "Setup Vibe-a-Gochi.bat" --voice      also installs speech
rem   "Setup Vibe-a-Gochi.bat" --uninstall  removes the Claude Code hooks
cd /d "%~dp0"
set PY=python
where python >nul 2>nul || set PY=py -3
%PY% --version >nul 2>nul || (
  echo Python is not installed. Get it from https://www.python.org/downloads/ ^(tick "Add to PATH"^), then run this again.
  pause
  exit /b 1
)
%PY% companion\setup.py %*
pause
