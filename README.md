# Setup Development Environment (Windows)
## Required Tools
- Visual Studio 2019 (16.3.0) 
- CMake 3.15 [https://cmake.org/download/]

## Checkout
- vcpkg
- vefs

## Run
(For this a ssh connection for the provided repositories has to be configured and the host has to be added to known_hosts file)
1. Build vcpkg: `bootstrap-vcpkg.bat -disableMetrics`
2. Replace both cmakeToolchain parameters in the CMakeSettings.template.json to the path of the vcpkg folder and save the file as CMakeSettings.json next to the template file.
3. Run vcpkg: `vcpkg install --triplet x64-windows-ltcg-static boost-outcome boost-exception boost-io boost-iterator boost-predef boost-preprocessor boost-program-options boost-system boost-test boost-uuid benchmark boringssl fmt libb2 protobuf`

## Finish
After the steps above it is possible to open the vefs folder inside VS 2019 and for example bin/vefs-tests.exe can be executed.
