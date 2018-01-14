@echo off
set ULTIMATE_IP=%1
if [%ULTIMATE_IP%]==[] set ULTIMATE_IP=192.168.1.203
echo.
echo Sending sidcrt.bin to Ultimate for IP: %ULTIMATE_IP%
echo.
python .\tools\sock.py s .\target\sidcrt.bin %ULTIMATE_IP%
