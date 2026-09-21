FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    python3 python3-pip python-is-python3 python3-pytest \
    dosfstools build-essential make ninja-build git \
    wget lsb-release software-properties-common gnupg \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    zip vim xxd uuid-dev \
    nasm acpica-tools libssl-dev bc bison flex \
    device-tree-compiler liblzma-dev\
    && rm -rf /var/lib/apt/lists/*

# apt.llvm.org's llvm.sh is a live upstream script: it dropped Ubuntu 22.04 and
# began refusing this image outright, which broke release builds without any
# change here. Configure the same pinned repository directly so the toolchain
# this image installs is a property of this file.
RUN wget -qO /usr/share/keyrings/llvm-snapshot.asc https://apt.llvm.org/llvm-snapshot.gpg.key && \
    echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.asc] https://apt.llvm.org/jammy/ llvm-toolchain-jammy-20 main" \
      > /etc/apt/sources.list.d/llvm-20.list && \
    apt-get update && apt-get install -y --no-install-recommends \
      clang-20 lld-20 llvm-20 llvm-20-dev llvm-20-tools libclang-rt-20-dev \
    && rm -rf /var/lib/apt/lists/*

RUN ln -s /usr/bin/clang-20 /usr/bin/clang && \
    ln -s /usr/bin/clang++-20 /usr/bin/clang++ && \
    ln -s /usr/bin/lld-20 /usr/bin/lld && \
    ln -s /usr/bin/llvm-ar-20 /usr/bin/llvm-ar && \
    ln -s /usr/bin/lld-20 /usr/bin/ld.lld && \
    ln -sf /usr/bin/llvm-objcopy-20 /usr/bin/llvm-objcopy && \
    ln -sf /usr/bin/llvm-strip-20 /usr/bin/llvm-strip

WORKDIR /workspace
