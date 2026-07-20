@echo off
setlocal

if not defined QT_ROOT set "QT_ROOT=E:\QT\6.8.2\msvc2022_64"
if not defined BF_PYTHON_EXECUTABLE set "BF_PYTHON_EXECUTABLE=%~dp0.venv\Scripts\python.exe"

set "PATH=%QT_ROOT%\bin;%PATH%"
set "APP=%~dp0native\build-windows\Release\bf_app.exe"

if not exist "%QT_ROOT%\bin\Qt6Core.dll" (
    echo Qt6 was not found at: %QT_ROOT%
    pause
    exit /b 1
)

if not exist "%BF_PYTHON_EXECUTABLE%" (
    echo Python sidecar environment was not found at: %BF_PYTHON_EXECUTABLE%
    pause
    exit /b 1
)

if not exist "%APP%" (
    echo BoundFree EEG has not been built: %APP%
    pause
    exit /b 1
)

pushd "%~dp0"
if "%~1"=="" (
    "%APP%"
) else (
    set "FIRST=%~1"
    call :run_with_argument %*
)
set "RESULT=%ERRORLEVEL%"
popd

if not "%RESULT%"=="0" pause
exit /b %RESULT%

:run_with_argument
if "%FIRST:~0,2%"=="--" (
    "%APP%" %*
) else (
    "%APP%" --open-file "%~f1"
)
exit /b %ERRORLEVEL%
