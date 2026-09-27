@echo off
setlocal

rem Build only the Release Win32 application and its DLL dependency.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio Installer was not found.
    exit /b 1
)

set "MSBUILD="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do (
    if not defined MSBUILD set "MSBUILD=%%I"
)

if not defined MSBUILD (
    echo [ERROR] Visual Studio C++ build tools were not found.
    exit /b 1
)

echo [BUILD] QuestEditor Release Win32
"%MSBUILD%" "%~dp0QuestEditorLauncher.vcxproj" /m /nologo /verbosity:minimal /p:Configuration=Release /p:Platform=Win32 /nodeReuse:false

if errorlevel 1 (
    echo [ERROR] Release build failed.
    exit /b 1
)

echo [DONE] Output: %~dp0bin\Win32\Release

@pause
exit /b 0
