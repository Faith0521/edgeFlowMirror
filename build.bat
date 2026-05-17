@echo off
echo Building edgeFlowMirror for Maya...

FOR %%G IN (2024, 2025) DO (call :build_for_maya "%%G")
GOTO :eof

:build_for_maya
set MAYA_VERSION=%1
set BUILD_DIR=build.%MAYA_VERSION%
echo Building for Maya %MAYA_VERSION%...

if exist %BUILD_DIR% (
    echo Cleaning build directory...
    rmdir /s /q %BUILD_DIR%
)

mkdir %BUILD_DIR%
cd %BUILD_DIR%

if %MAYA_VERSION% LSS "2022" (
    cmake -A x64 -T v140 -DMAYA_VERSION=%MAYA_VERSION% ../
) ELSE (
    cmake -A x64 -T v145 -DMAYA_VERSION=%MAYA_VERSION% ../
)

if %ERRORLEVEL% NEQ 0 (
    echo CMake configuration failed for Maya %MAYA_VERSION%
    cd ..
    goto :eof
)

cmake --build . --target install --config Release

if %ERRORLEVEL% NEQ 0 (
    echo Build failed for Maya %MAYA_VERSION%
) ELSE (
    echo Build completed successfully for Maya %MAYA_VERSION%
)

cd ..
echo.