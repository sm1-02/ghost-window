@echo off
setlocal ENABLEDELAYEDEXPANSION

set "CONFIG_FILE=build_config.txt"
set "GPP=g++"        REM try regular g++ first
set "USED_GPP="
set "CXXFLAGS="
set "LDFLAGS="

echo Checking for system g++...
where g++ >nul 2>&1
if %errorlevel%==0 (
    set "USED_GPP=g++"
    echo Found: g++ in PATH
) else (
    echo System g++ not found. Checking config file...

    if not exist "%CONFIG_FILE%" (
        echo [ERROR] No g++ in PATH and config file "%CONFIG_FILE%" missing.
        pause
        goto :eof
    )

    for /f "usebackq tokens=1,* delims==" %%A in ("%CONFIG_FILE%") do (
        set "KEY=%%A"
        set "VAL=%%B"
        if /I "!KEY!"=="GPP_PATH"  set "GPP=!VAL!"
        if /I "!KEY!"=="CXXFLAGS"  set "CXXFLAGS=!VAL!"
        if /I "!KEY!"=="LDFLAGS"   set "LDFLAGS=!VAL!"
    )

    if "%GPP%"=="g++" (
        echo [ERROR] No GPP_PATH defined in config file.
        pause
        goto :eof
    )

    if not exist "%GPP%" (
        echo [ERROR] g++ not found at:
        echo   %GPP%
        pause
        goto :eof
    )

    set "USED_GPP=%GPP%"
    echo Using g++ from config file:
    echo   %GPP%
)

echo.
echo CXXFLAGS:  %CXXFLAGS%
echo LDFLAGS:   %LDFLAGS%
echo.

if not exist "ghostwindow.cpp" (
    echo [ERROR] ghostwindow.cpp not found.
    pause
    goto :eof
)

"%USED_GPP%" ghostwindow.cpp %CXXFLAGS% -mwindows -lgdi32 -luser32 -lshell32 %LDFLAGS% -o GhostWindow.exe

echo.
if errorlevel 1 (
    echo [ERROR] Build FAILED.
) else (
    echo [OK] Build SUCCEEDED. Output: GhostWindow.exe
)

pause
endlocal
