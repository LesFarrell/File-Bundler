@echo off
setlocal
set "STRIP_EXE="

for /f "delims=" %%I in ('where strip 2^>nul') do if not defined STRIP_EXE set "STRIP_EXE=%%I"
if not defined STRIP_EXE (
    for /f "delims=" %%I in ('where llvm-strip 2^>nul') do if not defined STRIP_EXE set "STRIP_EXE=%%I"
)

if not exist application.rc (
    echo 1 ICON "application.ico" > application.rc
)
zig rc application.rc application.res
if errorlevel 1 exit /b 1
zig cc -target x86_64-windows-gnu -Oz -s -municode "-Wl,--subsystem,windows" -DUNICODE -D_UNICODE -o file_bundler.exe file_bundler.c application.res -lcabinet -lshell32 -lcomdlg32 -lole32 -lgdi32 -luser32
if errorlevel 1 exit /b 1
if defined STRIP_EXE (
    "%STRIP_EXE%" file_bundler.exe
    if errorlevel 1 exit /b 1
    echo Built and stripped file_bundler.exe for x64 using "%STRIP_EXE%"
) else (
    echo Built file_bundler.exe for x64
    echo Strip tool not found on PATH, skipping post-build strip.
)
