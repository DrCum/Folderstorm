@echo off
rem Source this from the viewer repo in cmd.exe so the session keeps the settings:
rem   call scripts\bootstrap-autobuild.cmd
rem
rem Creates .venv, installs requirements.txt, puts that venv's autobuild on PATH,
rem points AUTOBUILD_VARIABLES_FILE at fs-build-variables\variables, sets
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
