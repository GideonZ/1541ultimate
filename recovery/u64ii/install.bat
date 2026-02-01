@echo off
echo Start installation for Windows...

:: 1. Virtual Environment
if not exist .venv (
    echo Erstelle Virtual Environment...
    python -m venv .venv
) else (
    echo Virtual Environment existiert bereits.
)

:: 2. install libraries
echo Install Dependencies...
call .venv\Scripts\activate
pip install --upgrade pip
pip install -r requirements.txt

echo.
echo Installation completed!
echo Start the Recovery with run.bat
pause