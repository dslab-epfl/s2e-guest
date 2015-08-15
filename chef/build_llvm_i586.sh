#!/bin/bash
#
# Copyright 2015 EPFL. All rights reserved.

set -x
set -e

LLVM_BUILD_DIR=llvm.build
LLVM_INSTALL_DIR=$HOME/clang+llvm-3.6.2-i586

LLVM_SRC_DIR=llvm.src
CLANG_SRC_DIR=${LLVM_SRC_DIR}/tools/clang
COMPILER_RT_SRC_DIR=${LLVM_SRC_DIR}/projects/compiler-rt

LLVM_SRC_URL=http://llvm.org/releases/3.6.2/llvm-3.6.2.src.tar.xz
CLANG_SRC_URL=http://llvm.org/releases/3.6.2/cfe-3.6.2.src.tar.xz
COMPILER_RT_URL=http://llvm.org/releases/3.6.2/compiler-rt-3.6.2.src.tar.xz

if [ ! -d ${LLVM_SRC_DIR} ]; then
    mkdir -p ${LLVM_SRC_DIR}
    curl ${LLVM_SRC_URL} | tar -xJ -C ${LLVM_SRC_DIR} --strip-components=1
fi

if [ ! -d ${CLANG_SRC_DIR} ]; then
    mkdir -p ${CLANG_SRC_DIR}
    curl ${CLANG_SRC_URL} | tar -xJ -C ${CLANG_SRC_DIR} --strip-components=1
fi

if [ ! -d ${COMPILER_RT_SRC_DIR} ]; then
    mkdir -p ${COMPILER_RT_SRC_DIR}
    curl ${COMPILER_RT_URL} | tar -xJ -C ${COMPILER_RT_SRC_DIR} --strip-components=1
fi

if [ ! -d ${LLVM_BUILD_DIR} ]; then
    mkdir -p ${LLVM_BUILD_DIR}
    pushd ${LLVM_BUILD_DIR}
    cmake \
	-DCMAKE_C_FLAGS="-march=i586" \
	-DCMAKE_BUILD_TYPE=Release \
	-DLLVM_TARGETS_TO_BUILD='X86;CppBackend' \
	-DLLVM_ENABLE_ASSERTIONS=1 -DCMAKE_INSTALL_PREFIX=${LLVM_INSTALL_DIR} ../${LLVM_SRC_DIR} 
    popd
fi

pushd ${LLVM_BUILD_DIR}
make install -j$(nproc)
popd
