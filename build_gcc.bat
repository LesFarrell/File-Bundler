@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%" >nul || exit /b 1

set "BUILD_DIR=build\gcc\release"
set "OUTPUT=FileBundler.exe"
set "SRC_FILE=filebundler.c"
set "RC_FILE=application.rc"
set "ICON_FILE=application.ico"
set "RESOURCE_HEADER=resource.h"
set "RES_FILE=%BUILD_DIR%\application.res.o"
set "CFLAGS=-std=c11 -municode"
set "LDFLAGS=-municode -Wl,--subsystem,windows -lcabinet -lshell32 -lcomctl32 -lcomdlg32 -lgdi32 -lole32 -luser32"
set "RELEASE_CFLAGS=-DNDEBUG -Os -ffunction-sections -fdata-sections -fomit-frame-pointer"
set "RELEASE_LDFLAGS=-Wl,--gc-sections -Wl,--strip-all -s"
set "WINDRES=windres"

if /i "%~1"=="release" (
    rem Default settings already target the release GUI build.
) else if not "%~1"=="" (
    echo Usage: %~nx0 [release]
    popd >nul
    exit /b 1
)

where gcc >nul 2>nul || (
    echo gcc was not found in PATH.
    popd >nul
    exit /b 1
)

where "%WINDRES%" >nul 2>nul || (
    where x86_64-w64-mingw32-windres >nul 2>nul && set "WINDRES=x86_64-w64-mingw32-windres"
)

where "%WINDRES%" >nul 2>nul || (
    echo windres was not found in PATH.
    popd >nul
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%" || (
    echo Failed to create %BUILD_DIR%.
    popd >nul
    exit /b 1
)

if not exist "%SRC_FILE%" (
    echo Missing source file: %SRC_FILE%.
    popd >nul
    exit /b 1
)

if not exist "%RC_FILE%" (
    echo Missing resource script: %RC_FILE%.
    popd >nul
    exit /b 1
)

if not exist "%RESOURCE_HEADER%" (
    echo Missing resource header: %RESOURCE_HEADER%.
    popd >nul
    exit /b 1
)

if not exist "%ICON_FILE%" (
    echo Missing icon file: %ICON_FILE%.
    popd >nul
    exit /b 1
)

call "%WINDRES%" -I . -i "%RC_FILE%" -o "%RES_FILE%" || (
    popd >nul
    exit /b 1
)

call gcc %CFLAGS% %RELEASE_CFLAGS% -o "%OUTPUT%" "%SRC_FILE%" "%RES_FILE%" %LDFLAGS% %RELEASE_LDFLAGS% || (
    popd >nul
    exit /b 1
)

echo Built %OUTPUT% as a Windows GUI application with GCC.
popd >nul
exit /b 0
