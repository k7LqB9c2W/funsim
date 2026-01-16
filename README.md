cmake -B build -S . -A x64 -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build

If you use a vcpkg install outside Visual Studio, set VCPKG_ROOT and pass its toolchain file:
set VCPKG_ROOT=C:\Users\Jacob\vcpkg
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:\Users\Jacob\vcpkg\scripts\buildsystems\vcpkg.cmake
