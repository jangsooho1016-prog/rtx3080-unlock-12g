@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe" -std=c11 -O2 -Wall -Wextra -Werror -D_CRT_SECURE_NO_WARNINGS -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 tests\test_chainload.c chainload.c -o build\chainload-tests.exe
if errorlevel 1 exit /b 1
build\chainload-tests.exe
if errorlevel 1 exit /b 1
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\Llvm\x64\bin\clang.exe" -target x86_64-pc-windows-msvc -std=c11 -Os -ffreestanding -fno-builtin -fno-stack-protector -fshort-wchar -mno-red-zone -Wall -Wextra -Werror -Wno-unused-parameter -I ..\deps\edk2\MdePkg\Include -I ..\deps\edk2\MdePkg\Include\X64 -c chainload.c -o build\chainload-freestanding.obj
if errorlevel 1 exit /b 1
echo PASS freestanding x86_64-pc-windows-msvc compilation
exit /b 0
