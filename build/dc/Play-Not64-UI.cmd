@echo off
setlocal
cd /d "%~dp0"
if not exist "not64-ui.cdi" (
  echo Build the UI disc with build_ui_player.sh and package_ui_player.sh first.
  pause
  exit /b 1
)
"validation\flycast-2.6\flycast.exe" -config config:pvr.rend=0 -config config:rend.RenderToTextureBuffer=yes -config config:rend.EmulateFramebuffer=no -config config:Dynarec.Enabled=yes "%~dp0not64-ui.cdi"
