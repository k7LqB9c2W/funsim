Please always build when you make changes.
Use cmake to build when you can.
We are running on windows primarily
If you are in WSL/Linux but need to verify the Windows/Visual Studio build, use the
Windows `cmake.exe` and `powershell.exe` toolchain rather than the local Linux `cmake`
when the repo depends on Windows-only SDK/package resolution.

Example:
- Configure:
  `powershell.exe -NoProfile -Command "& 'C:\Program Files\CMake\bin\cmake.exe' -S 'C:\Users\Jacob\Documents\code\funsim' -B 'C:\Users\Jacob\Documents\code\funsim\build' -G 'Visual Studio 17 2022' -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/Users/Jacob/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows"`
- Build:
  `powershell.exe -NoProfile -Command "& 'C:\Program Files\CMake\bin\cmake.exe' --build 'C:\Users\Jacob\Documents\code\funsim\build' --config Release --target funsim"`

This produces `build/Release/funsim.exe` in the repo tree.
