@echo off
setlocal
cd /d "%~dp0build-win\Release"
if not exist Snap64RecompVR.exe (
  echo Build the Windows target with SNAP_ENABLE_VR=ON first.
  pause
  exit /b 1
)
Snap64RecompVR.exe --vr
