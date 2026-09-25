@echo off
rem Source this from the viewer repo in cmd.exe so the session keeps the settings:
rem   call scripts\bootstrap-autobuild.cmd
rem
rem Creates .venv, installs requirements.txt, puts that venv's autobuild on PATH,
rem puts Go on PATH when it is only in a usual install location, points
rem AUTOBUILD_VARIABLES_FILE at fs-build-variables\variables, sets
rem ProgramFiles(x86) when missing, and sets AUTOBUILD_VSVER=170 when both
rem Visual Studio 2022 and Visual Studio 2026 are installed.

setlocal EnableExtensions

if not defined ProgramFiles(x86) set "ProgramFiles(x86)=C:\Program Files (x86)"

for %%I in ("%~dp0..") do set "REPO_ROOT=%%~fI"
set "AUTOBUILD_VARIABLES_FILE=%REPO_ROOT%\fs-build-variables\variables"
if not exist "%AUTOBUILD_VARIABLES_FILE%" (
  echo bootstrap-autobuild: missing %AUTOBUILD_VARIABLES_FILE%
  exit /b 1
)

set "PY="
for %%V in (3.10 3.11 3.12 3.13) do (
  if not defined PY (
    py -%%V -c "import sys" >nul 2>&1 && set "PY=py -%%V"
  )
)
if not defined PY (
  python -c "import sys" >nul 2>&1 && set "PY=python"
)
if not defined PY (
  echo bootstrap-autobuild: Python was not found. Install Python 3.10-3.13 ^(py launcher^) or put python on PATH.
  exit /b 1
)

if not exist "%REPO_ROOT%\.venv\Scripts\python.exe" (
  %PY% -m venv "%REPO_ROOT%\.venv"
  if errorlevel 1 exit /b 1
)

call "%REPO_ROOT%\.venv\Scripts\activate.bat"
if errorlevel 1 exit /b 1

python -m pip install -r "%REPO_ROOT%\requirements.txt"
if errorlevel 1 exit /b 1

where autobuild >nul 2>&1
if errorlevel 1 (
  echo bootstrap-autobuild: autobuild is not on PATH after installing requirements.txt
  exit /b 1
)

rem Pin VS 2022 only when VS 2026 is also installed. Autobuild otherwise picks
rem the highest version it finds. Leave an existing AUTOBUILD_VSVER alone.
set "HAS_VS2022="
set "HAS_VS2026="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "delims=" %%V in ('"%VSWHERE%" -version "[17.0,18.0)" -products * -requires Microsoft.Component.MSBuild -property installationVersion 2^>nul') do set "HAS_VS2022=1"
  for /f "delims=" %%V in ('"%VSWHERE%" -version "[18.0,19.0)" -products * -requires Microsoft.Component.MSBuild -property installationVersion 2^>nul') do set "HAS_VS2026=1"
)
if not defined HAS_VS2022 if exist "%ProgramFiles%\Microsoft Visual Studio\2022" set "HAS_VS2022=1"
if not defined HAS_VS2022 if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022" set "HAS_VS2022=1"
if not defined HAS_VS2026 if exist "%ProgramFiles%\Microsoft Visual Studio\18" set "HAS_VS2026=1"
if not defined HAS_VS2026 if exist "%ProgramFiles%\Microsoft Visual Studio\2026" set "HAS_VS2026=1"
if defined HAS_VS2022 if defined HAS_VS2026 if not defined AUTOBUILD_VSVER set "AUTOBUILD_VSVER=170"

rem PATH first, then the same install locations as indra\cmake\GoToolchain.cmake.
rem Prepend that bin directory so configure works without a new terminal.
call :folderstorm_require_go
if errorlevel 1 exit /b 1

set "_PFX86=%ProgramFiles(x86)%"
set "_VSVER=%AUTOBUILD_VSVER%"
set "_ABVARS=%AUTOBUILD_VARIABLES_FILE%"
set "_PATH=%PATH%"
set "_VENV=%VIRTUAL_ENV%"
endlocal & set "PATH=%_PATH%" & set "VIRTUAL_ENV=%_VENV%" & set "AUTOBUILD_VARIABLES_FILE=%_ABVARS%" & set "ProgramFiles(x86)=%_PFX86%" & if not "%_VSVER%"=="" set "AUTOBUILD_VSVER=%_VSVER%"

echo AUTOBUILD_VARIABLES_FILE=%AUTOBUILD_VARIABLES_FILE%
where autobuild
if defined AUTOBUILD_VSVER echo AUTOBUILD_VSVER=%AUTOBUILD_VSVER%
exit /b 0

:folderstorm_require_go
setlocal EnableExtensions EnableDelayedExpansion
set "GO_EXE="
set "GO_FROM_PATH="
where go >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%G in ('where go 2^>nul') do (
    if not defined GO_EXE set "GO_EXE=%%G"
  )
  if defined GO_EXE set "GO_FROM_PATH=1"
)

if defined LOCALAPPDATA (
  if not defined GO_EXE if exist "!LOCALAPPDATA!\Programs\go\bin\go.exe" (
    set "GO_EXE=!LOCALAPPDATA!\Programs\go\bin\go.exe"
  )
)
if not defined GO_EXE if exist "C:\Program Files\Go\bin\go.exe" (
  set "GO_EXE=C:\Program Files\Go\bin\go.exe"
)
if defined ProgramFiles (
  if /I not "!ProgramFiles!\Go\bin"=="C:\Program Files\Go\bin" (
    if not defined GO_EXE if exist "!ProgramFiles!\Go\bin\go.exe" (
      set "GO_EXE=!ProgramFiles!\Go\bin\go.exe"
    )
  )
)

if not defined GO_EXE (
  echo bootstrap-autobuild: Go 1.25 or newer is required to build migrate-settings and fs-mcp.
  echo Searched PATH and:
  if defined LOCALAPPDATA echo   !LOCALAPPDATA!\Programs\go\bin
  echo   C:\Program Files\Go\bin
  if defined ProgramFiles if /I not "!ProgramFiles!\Go\bin"=="C:\Program Files\Go\bin" echo   !ProgramFiles!\Go\bin
  echo Install Go 1.25 or newer, or add it to PATH.
  endlocal
  exit /b 1
)

set "GO_VER_FILE=%TEMP%\folderstorm-go-version-%RANDOM%.txt"
"!GO_EXE!" version > "!GO_VER_FILE!" 2>&1
if errorlevel 1 (
  echo bootstrap-autobuild: '!GO_EXE! version' failed.
  type "!GO_VER_FILE!"
  echo Go 1.25 or newer is required to build migrate-settings and fs-mcp.
  del /q "!GO_VER_FILE!" >nul 2>&1
  endlocal
  exit /b 1
)
set "GO_VER_LINE="
for /f "usebackq delims=" %%V in ("!GO_VER_FILE!") do set "GO_VER_LINE=%%V"
del /q "!GO_VER_FILE!" >nul 2>&1

set "GO_VERTOK="
for %%T in (!GO_VER_LINE!) do (
  echo %%T | findstr /r /c:"^go[0-9][0-9]*\.[0-9][0-9]*" >nul && set "GO_VERTOK=%%T"
)
if not defined GO_VERTOK (
  echo bootstrap-autobuild: could not parse the Go version from '!GO_VER_LINE!' ^(!GO_EXE!^).
  echo Go 1.25 or newer is required to build migrate-settings and fs-mcp.
  endlocal
  exit /b 1
)

set "GO_VERNUM=!GO_VERTOK:go=!"
for /f "tokens=1,2 delims=." %%A in ("!GO_VERNUM!") do (
  set "GO_MAJOR=%%A"
  set "GO_MINOR_RAW=%%B"
)
set "GO_MINOR_NUM="
set "GO_MINOR_REST=!GO_MINOR_RAW!"
:folderstorm_go_minor_digits
if "!GO_MINOR_REST!"=="" goto folderstorm_go_minor_done
set "GO_MINOR_CHAR=!GO_MINOR_REST:~0,1!"
echo !GO_MINOR_CHAR! | findstr /r "[0-9]" >nul
if errorlevel 1 goto folderstorm_go_minor_done
set "GO_MINOR_NUM=!GO_MINOR_NUM!!GO_MINOR_CHAR!"
set "GO_MINOR_REST=!GO_MINOR_REST:~1!"
goto folderstorm_go_minor_digits
:folderstorm_go_minor_done
if not defined GO_MINOR_NUM set "GO_MINOR_NUM=0"
:folderstorm_go_strip_zero
if "!GO_MINOR_NUM!"=="0" goto folderstorm_go_compare
if "!GO_MINOR_NUM:~0,1!"=="0" (
  set "GO_MINOR_NUM=!GO_MINOR_NUM:~1!"
  goto folderstorm_go_strip_zero
)
:folderstorm_go_compare
set /a GO_MAJOR_N=!GO_MAJOR!
set /a GO_MINOR_N=!GO_MINOR_NUM!
if !GO_MAJOR_N! LSS 1 goto folderstorm_go_too_old
if !GO_MAJOR_N! EQU 1 if !GO_MINOR_N! LSS 25 goto folderstorm_go_too_old

echo go: !GO_EXE! ^(!GO_VER_LINE!^)
if defined GO_FROM_PATH (
  endlocal
  exit /b 0
)
for %%I in ("!GO_EXE!") do set "GO_BIN=%%~dpI"
endlocal & set "PATH=%GO_BIN%;%PATH%"
exit /b 0

:folderstorm_go_too_old
echo bootstrap-autobuild: found !GO_VER_LINE! at !GO_EXE!.
echo Go 1.25 or newer is required to build migrate-settings and fs-mcp.
echo Install Go 1.25 or newer, or add it to PATH.
endlocal
exit /b 1
