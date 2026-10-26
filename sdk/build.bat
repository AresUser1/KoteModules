@echo off
setlocal enabledelayedexpansion

rem ==============================================================================
rem Kotogram Module SDK Windows Build Script
rem Сборка модулей Kotogram под Windows для Android arm64-v8a и armeabi-v7a (32-bit)
rem ==============================================================================

set SCRIPT_DIR=%~dp0
set INCLUDE_DIR=%SCRIPT_DIR%include
set SRC_DIR=%SCRIPT_DIR%src

echo ==========================================================
echo    Kotogram Module SDK Builder (Windows)
echo ==========================================================

rem 1. Поиск NDK
if defined ANDROID_NDK_HOME (
    set NDK_ROOT=%ANDROID_NDK_HOME%
) else if defined ANDROID_NDK_ROOT (
    set NDK_ROOT=%ANDROID_NDK_ROOT%
) else if exist "C:\Android\android-ndk-r26c" (
    set NDK_ROOT=C:\Android\android-ndk-r26c
) else if defined ANDROID_HOME (
    for /d %%d in ("%ANDROID_HOME%\ndk\*") do set NDK_ROOT=%%d
) else if exist "%LOCALAPPDATA%\Android\Sdk\ndk" (
    for /d %%d in ("%LOCALAPPDATA%\Android\Sdk\ndk\*") do set NDK_ROOT=%%d
)

if not defined NDK_ROOT (
    echo [!] Ошибка: Android NDK не найден!
    echo Установите переменную ANDROID_NDK_HOME или NDK в Android Studio.
    exit /b 1
)

set CLANG_BIN=%NDK_ROOT%\toolchains\llvm\prebuilt\windows-x86_64\bin

rem Парсинг аргументов
set TARGET_ARCH=all
set TARGET_FILE=

:parse_args
if "%~1"=="" goto :start_build
if /i "%~1"=="--all" (
    set TARGET_ARCH=all
    shift
    goto :parse_args
)
if /i "%~1"=="--arm64" (
    set TARGET_ARCH=arm64
    shift
    goto :parse_args
)
if /i "%~1"=="--arm32" (
    set TARGET_ARCH=arm32
    shift
    goto :parse_args
)
set TARGET_FILE=%~1
shift
goto :parse_args

:start_build
echo [*] NDK: %NDK_ROOT%
echo [*] Выбранная архитектура: %TARGET_ARCH%

if /i "%TARGET_ARCH%"=="arm64" (
    call :build_arch arm64-v8a aarch64-linux-android26
) else if /i "%TARGET_ARCH%"=="arm32" (
    call :build_arch armeabi-v7a armv7a-linux-androideabi26
) else (
    call :build_arch arm64-v8a aarch64-linux-android26
    call :build_arch armeabi-v7a armv7a-linux-androideabi26
)

echo.
echo [OK] Сборка завершена!
exit /b 0

:build_arch
set "ARCH_NAME=%~1"
set "CLANG_PREFIX=%~2"
set "OUT_DIR=%SCRIPT_DIR%out\android-%ARCH_NAME%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

set "CC=%CLANG_BIN%\%CLANG_PREFIX%-clang.cmd"
set "CXX=%CLANG_BIN%\%CLANG_PREFIX%-clang++.cmd"
if not exist "%CC%" set "CC=%CLANG_BIN%\%CLANG_PREFIX%-clang.exe"
if not exist "%CXX%" set "CXX=%CLANG_BIN%\%CLANG_PREFIX%-clang++.exe"

if not exist "%CC%" (
    echo [!] Компилятор для %ARCH_NAME% не найден в %CLANG_BIN%
    exit /b 1
)

echo.
echo ---> Сборка под %ARCH_NAME%...

if not "%TARGET_FILE%"=="" (
    call :compile_file "%TARGET_FILE%" "%ARCH_NAME%" "%OUT_DIR%" "%CC%" "%CXX%"
    exit /b 0
)

for %%f in ("%SRC_DIR%\*.c") do call :compile_file "%%f" "%ARCH_NAME%" "%OUT_DIR%" "%CC%" "%CXX%"
for %%f in ("%SRC_DIR%\*.cpp") do call :compile_file "%%f" "%ARCH_NAME%" "%OUT_DIR%" "%CC%" "%CXX%"
for %%f in ("%SRC_DIR%\*.lua") do (
    echo   [LUA] Копирование %%~nxf...
    copy /y "%%f" "%OUT_DIR%\" >nul
)
exit /b 0

:compile_file
set "SRC_FILE=%~1"
set "CURR_ARCH=%~2"
set "CURR_OUT=%~3"
set "COMP_CC=%~4"
set "COMP_CXX=%~5"
set "EXT=%~x1"
set "NAME=%~n1"

echo   [BUILD] %~nx1 (%CURR_ARCH%)...
if /i "%EXT%"==".c" (
    "%COMP_CC%" -O2 -fPIC -shared -pthread -Wall -Wextra -std=c11 -Wl,-z,nodelete -I"%INCLUDE_DIR%" "%SRC_FILE%" -o "%CURR_OUT%\%NAME%.so"
) else (
    "%COMP_CXX%" -O2 -fPIC -shared -pthread -Wall -Wextra -std=c++20 -Wl,-z,nodelete -I"%INCLUDE_DIR%" "%SRC_FILE%" -o "%CURR_OUT%\%NAME%.so"
)
if %errorlevel% equ 0 (
    echo     Готово: %CURR_OUT%\%NAME%.so
) else (
    echo     [!] Ошибка компиляции %~nx1 (%CURR_ARCH%)
)
exit /b 0
