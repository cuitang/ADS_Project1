@echo off
setlocal
cd /d "%~dp0"
set "REPO=%~dp0.."
set "OUT=build\search.exe"
set "PORTER_OBJ=build\porter.o"

if not exist "build" mkdir "build"

echo Building Porter module...
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic ^
    -I "%REPO%\third_party\porter" ^
    -c "%REPO%\third_party\porter\porter.c" ^
    -o "%PORTER_OBJ%"
if errorlevel 1 goto :fail

echo Building search program...
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic ^
    -I src -I "%REPO%\includes" -I "%REPO%\third_party\porter" ^
    src\search.c src\dict.c src\text.c ^
    "%REPO%\src\index\AdsIndex.c" ^
    "%REPO%\src\preprocess\AdsPreprocess.c" ^
    "%PORTER_OBJ%" ^
    -o "%OUT%"
if errorlevel 1 goto :fail

echo Build complete: %~dp0%OUT%
echo Run from the project root, for example:
echo   "%~dp0build\search.exe" info --index data\test\index --stops data\test\demo_stopwords.txt
endlocal
exit /b 0

:fail
echo Build failed. Check the GCC output above.
endlocal
exit /b 1