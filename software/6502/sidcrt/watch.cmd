@echo off
:loop
.\tools\inotifywait.exe -qre modify .
make
echo.
echo Build finished at: %TIME:~0,2%:%TIME:~3,2%:%TIME:~6,2%
call .\tools\upload_sidcrt.cmd ".\target\sidcrt.bin" 192.168.1.203
goto loop