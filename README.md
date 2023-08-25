## 🚧 **UNDER CONSTRUCTION** 🚧

# Virtual Encrypted FileSystem

`libvefs` is a user-space C++ library for reading and writing `.vefs` files.
The `.vefs` file contains a virtual filesystem with transparent authenticated
encryption.

# Setup Development Environment
## Required Tools
- Visual Studio 2022 or G++ 11 or Clang 14
- `CMake` >= v3.22 [https://cmake.org/download/]
- vcpkg (containg at least [v2023.06.20](https://github.com/microsoft/vcpkg/releases/tag/2023.06.20))
    - Be sure to setup the environment variable `VCPKG_ROOT` with the local repository path

### devcontainer
The project comes with a devcontainer configuration, i.e. it is unnecessary to setup a Linux development environment.

## IDE support
The project comes with various `CMake` presets targetting the above compilers on Windows and Linux.
Therefore one can simply open the worktree with any IDE supporting `CMakePresets` (e.g. CLion, Visual Studio 2022 or Visual Studio Code)
