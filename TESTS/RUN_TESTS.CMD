@echo off
rem Build 26.11.12.1 02:10PM
rem RUN_TESTS.CMD - build and run the DS-DOS unit tests (SDK-free code only).
rem Run from the project root.  Exit code 0 = all tests passed.
setlocal
set VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat
if not exist "%VSDEV%" set VSDEV=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat
call "%VSDEV%" -arch=x86 -no_logo >nul
if not exist BUILD\tests mkdir BUILD\tests
cl /nologo /W3 /J /TC /Fo"BUILD\tests\\" /Fe"BUILD\tests\TEST_FNSPLIT.EXE" TESTS\TEST_FNSPLIT.C >BUILD\tests\build.log
if errorlevel 1 (type BUILD\tests\build.log & exit /b 1)
BUILD\tests\TEST_FNSPLIT.EXE
exit /b %errorlevel%
