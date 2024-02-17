set(CMAKE_SYSTEM_PROCESSOR AMD64)

execute_process(COMMAND brew --prefix llvm@16 OUTPUT_VARIABLE BREW_LLVM_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_C_COMPILER "${BREW_LLVM_PATH}/bin/clang")
set(CMAKE_CXX_COMPILER "${BREW_LLVM_PATH}/bin/clang++")
set(CMAKE_OSX_SYSROOT /Applications/Xcode_15.0.1.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS_INIT "-fsized-deallocation")
