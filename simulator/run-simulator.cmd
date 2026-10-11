@echo off
setlocal
set "PYTHONUTF8=1"
cd /d "%~dp0"
set "RM27_ENTRY=simulator\control_console.py"
if /I "%~1"=="--check" set "RM27_ENTRY=simulator\check_environment.py"
if defined RM27_PYTHON (
    "%RM27_PYTHON%" "%RM27_ENTRY%"
) else if exist "D:\YOLOV8\python.exe" (
    "D:\YOLOV8\python.exe" "%RM27_ENTRY%"
) else (
    python "%RM27_ENTRY%"
)
if errorlevel 1 pause
