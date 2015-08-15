#!/bin/bash
#
# Copyright 2015 EPFL. All rights reserved.

set -x
set -e

BUILD_DIR=build
LLVM_DIR=${HOME}/clang+llvm-3.6.2-i586

if [ ! -d ${BUILD_DIR} ]; then
    mkdir -p ${BUILD_DIR}

    pushd ${BUILD_DIR}
    # TODO: LLVM_DIR might not be needed if clang is on $PATH
    cmake \
	-DLLVM_DIR=${LLVM_DIR}/share/llvm/cmake/ \
	-DCMAKE_CXX_FLAGS="-march=i586" ..
    popd
fi

pushd ${BUILD_DIR}
make
popd
