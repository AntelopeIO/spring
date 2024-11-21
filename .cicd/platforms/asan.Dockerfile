# syntax=docker/dockerfile:1
FROM ubuntu:jammy
ENV TZ="America/New_York"
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y build-essential      \
                       cmake                \
                       git                  \
                       jq                   \
                       libcurl4-openssl-dev \
                       libgl-dev            \
                       libglx-dev           \
                       libopengl-dev        \
                       libwayland-dev       \
                       xorg-dev             \
                       libxkbcommon-dev     \
                       libxrandr-dev        \
                       libxinerama-dev      \
                       pkg-config           \
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

RUN yes | bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)" llvm.sh 19

#make sure no confusion on what llvm library spring's cmake should pick up on
RUN rm -rf /usr/lib/llvm-19/lib/cmake

ENV SPRING_PLATFORM_HAS_EXTRAS_CMAKE=1
COPY <<-EOF /extras.cmake
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)

  set(CMAKE_C_COMPILER "clang-19" CACHE STRING "")
  set(CMAKE_CXX_COMPILER "clang++-19" CACHE STRING "")
  set(CMAKE_C_FLAGS "-fsanitize=address -fno-omit-frame-pointer" CACHE STRING "")
  set(CMAKE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer" CACHE STRING "")
EOF

ENV ASAN_OPTIONS=detect_leaks=0
