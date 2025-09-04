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
                       libgmp-dev           \
                       llvm-11-dev          \
                       lsb-release          \
                       ninja-build          \
                       python3-numpy        \
                       software-properties-common \
                       file                 \
                       wget                 \
                       zlib1g-dev           \
                       zstd                 \
                       ;

ARG _LLVM_VERSION=21

RUN yes | bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)" llvm.sh ${_LLVM_VERSION}

#make sure no confusion on what llvm library spring's cmake should pick up on
RUN rm -rf /usr/lib/llvm-${_LLVM_VERSION}/lib/cmake

ENV SPRING_PLATFORM_HAS_EXTRAS_CMAKE=1
COPY <<-EOF /extras.cmake
	set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)

	set(CMAKE_C_COMPILER "clang-${_LLVM_VERSION}" CACHE STRING "")
	set(CMAKE_CXX_COMPILER "clang++-${_LLVM_VERSION}" CACHE STRING "")
	set(CMAKE_C_FLAGS "-fsanitize=address -fno-omit-frame-pointer" CACHE STRING "")
	set(CMAKE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer" CACHE STRING "")
EOF

ENV ASAN_OPTIONS=detect_leaks=0
