@echo off
rem Builds the public (Nexus) KotorPlus as a 32-bit dinput8.dll proxy.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out_public mkdir out_public
cl /nologo /O2 /MT /W3 /LD src\kotorplus.cpp /DKP_PUBLIC /Fo:out_public\ /Fe:out_public\dinput8.dll /link /DLL user32.lib gdi32.lib kernel32.lib || exit /b 1
echo BUILD OK
