@echo off
cd /d "%~dp0"
echo Pushing CPR_Zephyr_AED_Controller to GitHub...
echo Repository: https://github.com/SMARTSsprb/CPR_Zephyr_AED_Controller.git
echo.
git push -u origin master
if %errorlevel% equ 0 (
    echo.
    echo ====================================
    echo SUCCESS! Repository uploaded to GitHub.
    echo ====================================
) else (
    echo.
    echo Push failed or was cancelled.
)
echo.
pause
