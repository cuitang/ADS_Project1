@echo off
chcp 936 >nul
cd /d "%~dp0"
if not exist build mkdir build

set REPO=..\..\团队仓库_2026-10-14

echo 正在编译...
echo.
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic ^
    -I src -I "%REPO%\includes" -I "%REPO%\third_party\porter" ^
    src\search.c src\dict.c src\text.c ^
    "%REPO%\src\index\AdsIndex.c" ^
    "%REPO%\src\preprocess\AdsPreprocess.c" ^
    "%REPO%\third_party\porter\porter.c" ^
    -o build\search.exe
if errorlevel 1 (
    echo.
    echo 编译失败。
    pause
    exit /b 1
)
echo.
echo 编译完成：build\search.exe
echo.
echo 示例：
echo    build\search.exe info
echo    build\search.exe word king
echo    build\search.exe phrase king and queen
echo.
pause
