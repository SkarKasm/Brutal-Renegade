@echo off
call "E:\VisualStudio\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
set "PATH=E:\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;E:\VisualStudio\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S "%~dp0..\.." -B "D:\Command & Conquer Renegade(tm)\RenegadeVR Tools\build-renegade" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBR_BUILD_VOXEL_GORE_TOOLS=ON || exit /b 1
cmake --build "D:\Command & Conquer Renegade(tm)\RenegadeVR Tools\build-renegade" --target voxel_engine_parity --parallel 4 || exit /b 1
"D:\Command & Conquer Renegade(tm)\RenegadeVR Tools\build-renegade\bin\voxel_engine_parity.exe" "%~dp0output\input\body.w3d" "%~dp0output\input\hierarchy.w3d" "%~dp0output\engine_parity.txt" || exit /b 1
