@echo off
:: ============================================
:: Voices of the Void Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-asi.cmd. Copy this
:: file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change nothing
:: else. scripts/conformance.ps1 checks that nothing else changed.
::
:: Ultimate ASI Loader: one DLL renamed to the proxy the game already
:: imports, with the mod shipped as an .asi beside the game exe. Check the
:: exe's import table before choosing ASI_LOADER_NAME - a proxy the game does
:: not import is never loaded and the mod silently does nothing.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=voices-of-the-void"
set "MOD_DISPLAY_NAME=Voices of the Void Head Tracking"
set "MOD_DLLS=VoicesOfTheVoidHeadTracking.asi"
set "MOD_INTERNAL_NAME=VoicesOfTheVoidHeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
:: Filename the ASI loader DLL is renamed to: the import the game exe already
:: has. VotV-Win64-Shipping.exe imports WINMM.dll, dxgi.dll and XINPUT1_3.dll
:: statically and does not import dinput8.dll, so winmm.dll is the one of the
:: usual four that gets loaded here without sitting in the renderer path.
set "ASI_LOADER_NAME=winmm.dll"
:: Subdirectory below the exe directory to deploy into, for engines that load
:: their proxy DLL from somewhere other than beside the exe. Source engine wants
:: bin\; a copy next to the exe is never loaded. Leave empty for everything
:: else, and set the same value in uninstall.cmd.
::
:: Empty here: games.json resolves the exe to VotV\Binaries\Win64\, so the body
:: already deploys into that folder rather than the install root. The launcher
:: stub VotV.exe at the root is not the process the loader is injected into.
set "ASI_SUBDIR="
:: Files copied only when they are not already there, so an upgrade keeps
:: whatever the user tuned. Listing an .ini in MOD_DLLS instead puts it through
:: the unconditional copy and resets every key on every update.
::
:: Empty: the mod writes its own HeadTracking.ini on first run, so there is no
:: default file in the package to seed.
set "MOD_SEED_FILES="
:: Version of the vendored Ultimate ASI Loader, recorded in the state file so
:: the launcher can tell which loader build it is looking at. Leave empty to
:: omit the field. Bump alongside vendor/ via `pixi run update-deps`.
::
:: The vendored copy is v9.7.4 - see vendor/ultimate-asi-loader/README.md.
set "ASI_LOADER_VERSION=v9.7.4"
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls:&echo   End       - Toggle head tracking on/off&echo   Page Up   - Cycle tracking mode (full / rotation only / position only)&echo   Page Down - Toggle yaw mode (world-locked / camera-local)&echo.&echo Keyboards without a nav cluster:&echo   Ctrl+Shift+Y - Toggle head tracking&echo   Ctrl+Shift+G - Cycle tracking mode&echo   Ctrl+Shift+H - Toggle yaw mode"
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
