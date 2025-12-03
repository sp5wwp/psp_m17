#!/bin/bash

loc=`pwd`

#test if psp development environment is available
echo -n "Looking for psp-cmake... "
if ! command -v psp-cmake 2>&1 >/dev/null
then
    echo "not found. Exiting."
    exit 1
else
    echo "found!"
fi

#make sure PSPDEV is set
if test -z "${PSPDEV}"
then
    echo "The PSPDEV environment variable has not been set. Exiting."
    exit 1
fi

#test if codec2/build directory exists
echo "Building codec2"
if test -d codec2/build
then
    if ! test -f codec2/build/src/libcodec2.so
    then
        cd codec2/build
        cmake .. && make -j`nproc`
        echo "codec2 built." #TODO: check the real result
    else
        echo "codec2 seems to be built already."
    fi
else
    mkdir codec2/build && cd codec2/build
    cmake .. && make -j`nproc`
    echo "codec2 built." #TODO: check the real result
fi

#build the whole app
echo "Building psp_m17"
cd $loc
if test -d build
then
    #if ! test -f build/psp_m17
    #then
        cd build
        psp-cmake .. && make -j`nproc`
        echo "psp_m17 built. All done." #TODO: check the real result
    #else
        #echo "psp_m17 seems to be built already. Exiting."
    #fi
else
    mkdir build && cd build
    psp-cmake .. && make -j`nproc`
    echo "psp_m17 built. All done." #TODO: check the real result
fi
