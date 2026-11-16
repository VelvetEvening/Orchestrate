@echo off
rem Emergency cancel: close the countdown window and abort a queued shutdown.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0shutdown-control.ps1" -Action cancel
pause
