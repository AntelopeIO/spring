# syntax=docker/dockerfile:1
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
                       lsb-release          \
                       ninja-build          \
                       python3-numpy        \
                       software-properties-common \
                       file                 \
                       wget                 \
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

RUN yes | bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)" llvm.sh 19

#make sure no confusion on what llvm library spring's cmake should pick up on
RUN rm -rf /usr/lib/llvm-19/lib/cmake

COPY <<-EOF /ubsan.supp
  vptr:wasm_eosio_validation.hpp
  vptr:wasm_eosio_injection.hpp
EOF

ENV SPRING_PLATFORM_HAS_EXTRAS_CMAKE=1
COPY <<-EOF /extras.cmake
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)

  set(CMAKE_C_COMPILER "clang-19" CACHE STRING "")
  set(CMAKE_CXX_COMPILER "clang++-19" CACHE STRING "")
  set(CMAKE_C_FLAGS "-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" CACHE STRING "")
  set(CMAKE_CXX_FLAGS "-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" CACHE STRING "")
EOF

ENV UBSAN_OPTIONS=print_stacktrace=1,suppressions=/ubsan.supp
