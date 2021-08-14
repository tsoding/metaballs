@REM https://github.com/cmuratori/refterm/blob/main/build.bat
@echo off

where /q cl || (
  echo ERROR: "cl" not found - please run this from the MSVC x64 native tools command prompt
  exit /b 1
)

call cl /O2 /Fe:metaballs /nologo /W3 main.c /link User32.lib Gdi32.lib /subsystem:windows

dir metaballs.exe
