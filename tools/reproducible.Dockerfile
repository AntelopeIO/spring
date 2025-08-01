# syntax=docker/dockerfile:1
# debian:buster on Sep 20 2023
FROM debian@sha256:d774a984460a74973e6ce4d1f87ab90f2818e41fcdd4802bcbdc4e0b67f9dadf AS builder

# If enabling the snapshot repo below, this ought to be after the base image time from above.
# date -u -d @1695620708 = Mon Sep 25 05:45:08 AM UTC 2023
ENV SOURCE_DATE_EPOCH=1695620708

# The snapshot repo is currently disabled due to poor performance. Re-eval in the future.
# When the package repo is signed, a message in the payload indicates the time when the repo becomes stale. This protection
#  nominally exists to ensure older versions of the package repo which may contain defective packages aren't served in the far
#  future. But in our case, we want this pinned package repo at any future date. So [check-valid-until=no] to disable this check.
##RUN <<EOF
##cat <<EOS > /etc/apt/sources.list
##deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/$(date -d @${SOURCE_DATE_EPOCH} +%Y%m%dT%H%M%SZ)/ buster main
##deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/$(date -d @${SOURCE_DATE_EPOCH} +%Y%m%dT%H%M%SZ)/ buster/updates main
##EOS
##EOF

RUN apt-get update && apt-get -y upgrade && DEBIAN_FRONTEND=noninteractive apt-get -y install build-essential \
                                                                                              file \
                                                                                              git \
                                                                                              libcurl4-openssl-dev \
                                                                                              libgmp-dev \
                                                                                              ninja-build \
                                                                                              python3 \
                                                                                              zlib1g-dev \
                                                                                              zstd \
                                                                                              ;

ARG _SPRING_CLANG_VERSION=18.1.8
ARG _SPRING_LLVM_VERSION=11.1.0
ARG _SPRING_CMAKE_VERSION=3.27.6

ADD https://github.com/llvm/llvm-project/releases/download/llvmorg-${_SPRING_CLANG_VERSION}/llvm-project-${_SPRING_CLANG_VERSION}.src.tar.xz     \
    https://github.com/llvm/llvm-project/releases/download/llvmorg-${_SPRING_CLANG_VERSION}/llvm-project-${_SPRING_CLANG_VERSION}.src.tar.xz.sig \
    https://github.com/llvm/llvm-project/releases/download/llvmorg-${_SPRING_LLVM_VERSION}/llvm-project-${_SPRING_LLVM_VERSION}.src.tar.xz       \
    https://github.com/llvm/llvm-project/releases/download/llvmorg-${_SPRING_LLVM_VERSION}/llvm-project-${_SPRING_LLVM_VERSION}.src.tar.xz.sig   \
    https://github.com/Kitware/CMake/releases/download/v${_SPRING_CMAKE_VERSION}/cmake-${_SPRING_CMAKE_VERSION}.tar.gz                           \
    https://github.com/Kitware/CMake/releases/download/v${_SPRING_CMAKE_VERSION}/cmake-${_SPRING_CMAKE_VERSION}-SHA-256.txt                      \
    https://github.com/Kitware/CMake/releases/download/v${_SPRING_CMAKE_VERSION}/cmake-${_SPRING_CMAKE_VERSION}-SHA-256.txt.asc                  \
    /

# CBA23971357C2E6590D9EFD3EC8FEF3A7BFB4EDA - Brad King <brad.king@kitware.com> (cmake)
# 474E22316ABF4785A88C6E8EA2C794A986419D8A - Tom Stellard <tstellar@redhat.com> (llvm)
# D574BD5D1D0E98895E3BF90044F2485E45D59042 - Tobias Hieta <tobias@hieta.se> (llvm)

RUN gpg --keyserver hkps://keyserver.ubuntu.com --recv-keys CBA23971357C2E6590D9EFD3EC8FEF3A7BFB4EDA \
                                                            474E22316ABF4785A88C6E8EA2C794A986419D8A \
                                                            D574BD5D1D0E98895E3BF90044F2485E45D59042

RUN ls *.sig *.asc | xargs -n 1 gpg --verify && \
    sha256sum -c --ignore-missing cmake-*-SHA-256.txt

RUN tar xf cmake-*.tar.gz && \
    cd cmake*[0-9] && \
    echo 'set(CMAKE_USE_OPENSSL OFF CACHE BOOL "" FORCE)' > spring-init.cmake && \
    ./bootstrap --parallel=$(nproc) --init=spring-init.cmake --generator=Ninja && \
    ninja install && \
    rm -rf cmake*

RUN tar xf llvm-project-${_SPRING_CLANG_VERSION}.src.tar.xz && \
    cmake -S llvm-project-${_SPRING_CLANG_VERSION}.src/llvm -B build-toolchain -GNinja -DLLVM_INCLUDE_DOCS=Off -DLLVM_TARGETS_TO_BUILD=host -DCMAKE_BUILD_TYPE=Release \
                                                                                     -DCMAKE_INSTALL_PREFIX=/pinnedtoolchain \
                                                                                     -DCOMPILER_RT_BUILD_SANITIZERS=Off \
                                                                                     -DLIBCXX_HARDENING_MODE=fast \
                                                                                     -DLLVM_ENABLE_PROJECTS='lld;clang;clang-tools-extra' \
                                                                                     -DLLVM_ENABLE_RUNTIMES='compiler-rt;libc;libcxx;libcxxabi;libunwind' && \
    cmake --build build-toolchain -t install && \
    rm -rf build*

COPY <<-"EOF" /pinnedtoolchain/pinnedtoolchain.cmake
	set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/bin/clang)
	set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/bin/clang++)

	set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_LIST_DIR}/include/c++/v1 ${CMAKE_CURRENT_LIST_DIR}/include/x86_64-unknown-linux-gnu/c++/v1 /usr/local/include /usr/include)

	foreach(v CMAKE_C_FLAGS_RELEASE_INIT        CMAKE_CXX_FLAGS_RELEASE_INIT
	          CMAKE_C_FLAGS_RELWITHDEBINFO_INIT CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT)
	   set(${v} "-D_FORTIFY_SOURCE=2 -fstack-protector-strong")
	endforeach()

	set(CMAKE_C_FLAGS_INIT "-fpie -pthread")
	set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -fpie -pthread")

	set(CMAKE_EXE_LINKER_FLAGS_INIT "-stdlib=libc++ -nostdlib++ -pie -pthread -Wl,-z,relro,-z,now")
	set(CMAKE_SHARED_LINKER_FLAGS_INIT "-stdlib=libc++ -nostdlib++")
	set(CMAKE_MODULE_LINKER_FLAGS_INIT "-stdlib=libc++ -nostdlib++")

	set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CURRENT_LIST_DIR}/lib/x86_64-unknown-linux-gnu/libc++.a ${CMAKE_CURRENT_LIST_DIR}/lib/x86_64-unknown-linux-gnu/libc++abi.a")

	set(LLVM_ROOT "${CMAKE_CURRENT_LIST_DIR}/pinllvm")
EOF
ENV CMAKE_TOOLCHAIN_FILE=/pinnedtoolchain/pinnedtoolchain.cmake

RUN tar xf llvm-project-${_SPRING_LLVM_VERSION}.src.tar.xz && \
    cmake -S llvm-project-${_SPRING_LLVM_VERSION}.src/llvm -B build-pinllvm -GNinja -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=host -DLLVM_BUILD_TOOLS=Off \
                                                                                  -DLLVM_ENABLE_RTTI=On -DLLVM_ENABLE_TERMINFO=Off -DLLVM_ENABLE_PIC=Off \
                                                                                  -DCMAKE_INSTALL_PREFIX=/pinnedtoolchain/pinllvm && \
    cmake --build build-pinllvm -t install && \
    rm -rf build* llvm*

# It's possible to extract the toolchain to the host using this target by something like,
#  docker build --target export-toolchain -o $HOME/springtoolchain/ -f tools/reproducible.Dockerfile .
# and then use it for building spring by something like,
#  cmake -DCMAKE_TOOLCHAIN_FILE=$HOME/springtoolchain/pinnedtoolchain.cmake -D....
# The build won't be reproducible, but it may help it troubleshooting errors, warnings, or bugs with the pinned compiler
FROM scratch AS export-toolchain
COPY --from=builder /pinnedtoolchain/ /

FROM builder AS build

ARG SPRING_BUILD_JOBS

# Yuck: This places the source at the same location as spring's CI (build.yaml, build_base.yaml). Unfortunately this location only matches
#       when build.yaml etc are being run from a repository named spring.
COPY / /__w/spring/spring
RUN cmake -S /__w/spring/spring -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -GNinja && \
    cmake --build build -t package -- ${SPRING_BUILD_JOBS:+-j$SPRING_BUILD_JOBS} && \
    /__w/spring/spring/tools/tweak-deb.sh build/antelope-spring_*.deb

FROM scratch AS exporter
COPY --from=build /build/*.deb /build/*.tar.* /
