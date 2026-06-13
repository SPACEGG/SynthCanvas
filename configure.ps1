cmake -B build/android -G Ninja `
  -DCMAKE_C_COMPILER_LAUNCHER=sccache `
  -DCMAKE_CXX_COMPILER_LAUNCHER=sccache `
  -DCMAKE_TOOLCHAIN_FILE="${env:ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a `
  -DANDROID_PLATFORM=android-33 `
  -DCMAKE_BUILD_TYPE=Release `
  -DGODOTCPP_DISABLE_EXCEPTIONS=OFF `
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON `
  -DCMAKE_CXX_CLANG_TIDY="clang-tidy" `
  .

if ($?) {
    (Get-Content build/android/compile_commands.json) | ForEach-Object {
        $_ -replace 'C:\\\\', 'c:\\\\' -replace 'C:/', 'c:/'
    } | Set-Content build/android/compile_commands.json -Encoding UTF8
}