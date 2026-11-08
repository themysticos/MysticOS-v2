@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM  MysticOS 2 launcher - runs the OS in a window on Windows.
REM  Double-click this file. Needs QEMU for Windows installed.
REM ============================================================

title MysticOS 2
cd /d "%~dp0"

REM --- Locate qemu-system-x86_64.exe ---
set QEMU=
for %%P in (qemu-system-x86_64.exe) do if not defined QEMU set QEMU=%%~$PATH:P
if not defined QEMU if exist "C:\Program Files\qemu\qemu-system-x86_64.exe" set QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe
if not defined QEMU if exist "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe" set QEMU=C:\Program Files (x86)\qemu\qemu-system-x86_64.exe
if not defined QEMU (
  echo [ERROR] qemu-system-x86_64.exe not found.
  echo Install QEMU for Windows or put it in PATH.
  pause
  exit /b 1
)

REM --- Check images ---
if not exist "mysticos2.img" (
  echo [ERROR] mysticos2.img not found. Build it first: make
  pause
  exit /b 1
)

REM --- Options ---
REM   full      = fullscreen
REM   snapshot  = read-only run (changes not saved to image)
REM   whpx      = try hardware accel (fast, but may show a black screen
REM               on some systems - then it does NOT auto-fallback,
REM               because QEMU stays alive; use plain run instead)
set EXTRA=
set ACCEL=-accel whpx
if /I "%~1"=="full"     set EXTRA=-full-screen
if /I "%~1"=="snapshot" set EXTRA=-snapshot
if /I "%~1"=="whpx"     set ACCEL=-accel whpx
if /I "%~2"=="full"     set EXTRA=-full-screen
if /I "%~2"=="snapshot" set EXTRA=-snapshot

REM --- Second disk (optional) ---
set DISK2=
if exist "disk2.img" set DISK2=-drive file=disk2.img,format=raw,media=disk,if=ide

REM ============================================================
REM  Launch. TCG (pure emulation) is the reliable default.
REM  Add "whpx" argument to try hardware acceleration instead.
REM ============================================================
echo Starting MysticOS 2 (accelerator: %ACCEL%) ...

"%QEMU%" %ACCEL% -m 512 -name "MysticOS 2" -rtc base=localtime ^
  -drive file=mysticos2.img,format=raw,media=disk,if=ide -device intel-hda -device hda-duplex ^
  %DISK2% -device intel-hda -device hda-duplex %EXTRA%

endlocal
