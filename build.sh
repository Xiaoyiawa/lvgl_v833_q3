#!/bin/bash

make clean

export TOOLCHAIN=/opt/musl-v833
export PATH=$PATH:$TOOLCHAIN/bin
export CC=$TOOLCHAIN/bin/arm-linux-musleabihf-gcc
export CXX=$TOOLCHAIN/bin/arm-linux-musleabihf-g++
export AR=$TOOLCHAIN/bin/arm-linux-musleabihf-ar
export RANLIB=$TOOLCHAIN/bin/arm-linux-musleabihf-ranlib
export STRIP=$TOOLCHAIN/bin/arm-linux-musleabihf-strip

export STAGING_DIR=$TOOLCHAIN/bin

make -j${nproc}
