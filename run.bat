@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64 > nul
cmake -B out/build/x64-Debug -G Ninja && cmake --build out/build/x64-Debug && out\build\x64-Debug\openGLstudy.exe
