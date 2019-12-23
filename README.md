# Setup Development Environment (Windows)
## Required Tools
- Visual Studio 2019 (tested with 16.4.0) 
- CMake 3.15 [https://cmake.org/download/]

## Checkout additionally
- clone this repository
- vefs-packages (https://github.com/BurningEnlightenment/vefs-packages)


## Run
(For this a ssh connection for the provided repositories has to be configured and the host has to be added to known_hosts file)
1. Build vcpkg-packages in the vcpkg-packges folder run in PowerShell: `.\build.ps`
2. Replace both cmakeToolchain parameters in the CMakeSettings.template.json to the path of the vcpkg folder (inside the vcpkg-packages folder) and save the file as CMakeSettings.json next to the template file.

## Finish
After the steps above it is possible to open the vefs folder inside VS 2019 and for example bin/vefs-tests.exe can be executed.
