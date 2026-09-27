@echo off
setlocal
rem Set the complete game installation here. No game copy is created.
set "GAME_DIR=E:\coding_projects\c_projects\quest_editor\samples\AS2R full\AlienShooter2 Reloaded"
set "PROJECT_DIR=%~dp0.."
set "MODE=%~1"
if not defined MODE set "MODE=all"
set "CASE_FILTER=%~2"
if not defined CASE_FILTER set "CASE_FILTER=*"
set "RESULT=1"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    call :fail "Visual Studio Installer was not found"
    goto finish
)
set "MSBUILD="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do (
    if not defined MSBUILD set "MSBUILD=%%I"
)
if not defined MSBUILD (
    call :fail "Visual Studio C++ build tools were not found"
    goto finish
)
"%MSBUILD%" "%PROJECT_DIR%\quest_editor_tests.vcxproj" /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=Win32 /nodeReuse:false
if errorlevel 1 (
    call :fail "Release build"
    goto finish
)
rem The runner prints every named result to both the console and run.log.
"%PROJECT_DIR%\bin\Win32\Release\quest_editor_tests.exe" "%MODE%" "%PROJECT_DIR%" "%GAME_DIR%" "%CASE_FILTER%"
set "RESULT=%ERRORLEVEL%"
goto finish

:fail
powershell.exe -NoLogo -NoProfile -Command "Write-Host '[FAIL] %~1' -ForegroundColor Red"
exit /b 0

:finish
@pause
exit /b %RESULT%
