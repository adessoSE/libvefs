FROM mcr.microsoft.com/devcontainers/base:ubuntu-22.04

USER root
SHELL [ "/bin/bash", "-c" ]

RUN export DEBIAN_FRONTEND=noninteractive; \
    apt-get -qq update && apt-get -qq install --no-install-recommends \
    bash-completion \
    build-essential \
    clang-15 \
    clang-format-15 \
    clang-tidy-15 \
    curl \
    g++-12 \
    gcc-12 \
    gdb \
    git \
    lldb-15 \
    llvm-15 \
    ninja-build \
    pkg-config \
    python3 \
    python3-pip \
    tar \
    unzip \
    zip \
    zsh \
    && apt-get -qq autoremove \
    && apt-get -qq clean \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 120 --slave /usr/bin/g++ g++ /usr/bin/g++-12 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-15 150 --slave /usr/bin/clang++ clang++ /usr/bin/clang++-15

ARG CMAKE_VERSION="3.22.6"
RUN declare -A archs=( ["amd64"]="x86_64" ["arm64"]="aarch64" ); \
    architecture=$(dpkg --print-architecture); \
    echo "Detected arch: $architecture => ${archs[$architecture]}"; \
    [[ "${archs[$architecture]}" != "" ]] \
    && mkdir -p /opt/cmake \
    && CMAKE_BINARY_NAME="cmake-$CMAKE_VERSION-linux-${archs[$architecture]}.sh" \
    && TMP_DIR=$(mktemp -d -t cmake-XXXXXXXXXX) \
    && pushd "$TMP_DIR" \
    && curl -sSL "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/$CMAKE_BINARY_NAME" -O \
    && sh "$TMP_DIR/$CMAKE_BINARY_NAME" --prefix=/opt/cmake --skip-license \
    && popd \
    && rm -rf "$TMP_DIR" \
    && ln -s -t /usr/local/bin /opt/cmake/bin/cmake /opt/cmake/bin/ccmake /opt/cmake/bin/ctest /opt/cmake/bin/cpack

# Setup ENV vars for vcpkg
ENV VCPKG_ROOT=/usr/local/vcpkg \
    VCPKG_NO_CI=1
RUN mkdir -p "$VCPKG_ROOT" \
    && chown vscode:vscode "$VCPKG_ROOT" \
    && su vscode -c "git clone -c core.eol=lf -c core.autocrlf=false -c fsck.zeroPaddedFilemode=ignore -c fetch.fsck.zeroPaddedFilemode=ignore -c receive.fsck.zeroPaddedFilemode=ignore https://github.com/microsoft/vcpkg $VCPKG_ROOT" \
    && VCPKG_DOWNLOADS="$HOME/.cache/vcpkg/downloads" \
    && sh "$VCPKG_ROOT/bootstrap-vcpkg.sh" \
    && rm -rf "$VCPKG_DOWNLOADS" \
    && ln -s "$VCPKG_ROOT/vcpkg" "/usr/local/bin/vcpkg" \
    && echo 'export VCPKG_DOWNLOADS="$HOME/.cache/vcpkg/downloads"' >> ~vscode/.zshenv \
    && echo 'export VCPKG_DOWNLOADS="$HOME/.cache/vcpkg/downloads"' >> ~vscode/.bash_profile

USER vscode

RUN python3 -m pip install --user pipenv
