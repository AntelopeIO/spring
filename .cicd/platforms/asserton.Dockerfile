# syntax=docker/dockerfile:1
#the exact version of Ubuntu doesn't matter for the purpose of asserton builds. Feel free to upgrade in future
FROM ubuntu:jammy
ENV TZ="America/New_York"
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y build-essential      \
                       git                  \
                       jq                   \
                       libcurl4-openssl-dev \
                       libgmp-dev           \
                       llvm-11-dev          \
                       ninja-build          \
                       python3-numpy        \
                       file                 \
                       zlib1g-dev           \
                       zstd

#want to use cmake 3.27+ for TIMEOUT_SIGNAL_NAME. Once upgrading to 24.04 this can be removed
ADD https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5.tar.gz                           \
    https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-SHA-256.txt                      \
    https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-SHA-256.txt.asc                  \
    /
# CBA23971357C2E6590D9EFD3EC8FEF3A7BFB4EDA - Brad King <brad.king@kitware.com> (cmake)
RUN gpg --keyserver hkps://keyserver.ubuntu.com --recv-keys CBA23971357C2E6590D9EFD3EC8FEF3A7BFB4EDA
RUN gpg --verify cmake-3.30.5-SHA-256.txt.asc && \
    sha256sum -c --ignore-missing cmake-*-SHA-256.txt
RUN tar xf cmake-*.tar.gz && \
    cd cmake*[0-9] && \
    echo 'set(CMAKE_USE_OPENSSL OFF CACHE BOOL "" FORCE)' > spring-init.cmake && \
    ./bootstrap --parallel=$(nproc) --init=spring-init.cmake --generator=Ninja && \
    ninja install && \
    rm -rf cmake*

ENV SPRING_PLATFORM_HAS_EXTRAS_CMAKE=1
COPY <<-EOF /extras.cmake
  # reset the build type to empty to disable any cmake default flags
  set(CMAKE_BUILD_TYPE "" CACHE STRING "" FORCE)

  set(CMAKE_C_FLAGS "-O3" CACHE STRING "")
  set(CMAKE_CXX_FLAGS "-O3" CACHE STRING "")

  set(SPRING_ENABLE_RELEASE_BUILD_TEST "Off" CACHE BOOL "")
EOF
