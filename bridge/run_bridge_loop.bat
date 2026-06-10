@echo off
REM Auto-restarting launcher for the CTRL_Weight bridge.
REM Keeps the serial->Supabase forwarder alive across crashes / Arduino unplugs.
cd /d "%~dp0"
set PY=%LOCALAPPDATA%\Programs\Python\Python312\python.exe

:loop
echo [%date% %time%] starting bridge.py >> "%~dp0_bridge_service.log"
"%PY%" bridge.py >> "%~dp0_bridge_service.log" 2>&1
echo [%date% %time%] bridge.py exited, restarting in 5s >> "%~dp0_bridge_service.log"
timeout /t 5 /nobreak > nul
goto loop
