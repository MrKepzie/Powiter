#!/bin/bash

#Usage PLUGINDIR="..." MKJOBS=4 CONFIG=relwithdebinfo BRANCH=workshop ./build-plugins.sh

source $(pwd)/common.sh || exit 1

cd $CWD/build || exit 1

#If "workshop" is passed, use master branch for all plug-ins otherwise use the git tags in common.sh
if [ "BRANCH" == "workshop" ]; then
    IO_BRANCH=master
    MISC_BRANCH=master
    ARENA_BRANCH=master
    CV_BRANCH=master
else
    IO_BRANCH=$IOPLUG_GIT_TAG
    MISC_BRANCH=$MISCPLUG_GIT_TAG
    ARENA_BRANCH=$ARENAPLUG_GIT_TAG
    CV_BRANCH=$CVPLUG_GIT_TAG
fi

if [ ! -d $PLUGINDIR ]; then
    echo "Error: plugin directory '$PLUGINDIR' does not exist"
    exit 1
fi

#Build openfx-io
git clone $GIT_IO || exit 1
cd openfx-io || exit 1
git checkout "$IO_BRANCH" || exit 1
git submodule update -i --recursive || exit 1

#Always bump git commit, it is only used to version-stamp binaries
IO_GIT_VERSION=$(git log|head -1|awk '{print $2}')
sed -i "s/IOPLUG_DEVEL_GIT=.*/IOPLUG_DEVEL_GIT=${IO_GIT_VERSION}/" $CWD/commits-hash.sh || exit 1

make CXX=clang++ BITS=$BITS CONFIG=$CONFIG OCIO_HOME=/opt/local OIIO_HOME=/opt/local SEEXPR_HOME=/opt/local -j${MKJOBS} || exit 1
mv IO/$OS-$BITS-$CONFIG/IO.ofx.bundle "$PLUGINDIR" || exit 1
cd ..

#Build openfx-misc
git clone $GIT_MISC || exit 1
cd openfx-misc || exit 1
git checkout "$MISC_BRANCH" || exit 1
git submodule update -i --recursive || exit 1

#Always bump git commit, it is only used to version-stamp binaries
MISC_GIT_VERSION=$(git log|head -1|awk '{print $2}')
sed -i "s/MISCPLUG_DEVEL_GIT=.*/MISCPLUG_DEVEL_GIT=${MISC_GIT_VERSION}/" $CWD/commits-hash.sh || exit 1


make CXX=clang++ BITS=$BITS HAVE_CIMG=1 CONFIG=$CONFIG -j${MKJOBS} || exit 1
mv Misc/$OS-$BITS-$CONFIG/Misc.ofx.bundle "$PLUGINDIR" || exit 1
mv CImg/$OS-$BITS-$CONFIG/CImg.ofx.bundle "$PLUGINDIR" || exit 1
cd ..

#Build openfx-arena
git clone $GIT_ARENA || exit 1
cd openfx-arena || exit 1
git checkout "$ARENA_BRANCH" || exit 1
git submodule update -i --recursive || exit 1

#Always bump git commit, it is only used to version-stamp binaries
ARENA_GIT_VERSION=$(git log|head -1|awk '{print $2}')
sed -i "s/ARENAPLUG_DEVEL_GIT=.*/ARENAPLUG_DEVEL_GIT=${ARENA_GIT_VERSION}/" $CWD/commits-hash.sh || exit 1

make CXX=clang++ USE_PANGO=1 USE_SVG=1 BITS=$BITS CONFIG=$CONFIG -j${MKJOBS} || exit 1
mv Bundle/$OS-$BITS-$CONFIG/Arena.ofx.bundle "$PLUGINDIR" || exit 1
cd ..


#Build openfx-opencv
#git clone $GIT_OPENCV || exit 1
#cd openfx-opencv || exit 1
#git checkout "$CV_BRANCH" || exit 1
#git submodule update -i --recursive || exit 1

#Always bump git commit, it is only used to version-stamp binaries
#CV_GIT_VERSION=$(git log|head -1|awk '{print $2}')
#sed -i "s/CVPLUG_DEVEL_GIT=.*/CVPLUG_DEVEL_GIT=${CV_GIT_VERSION}/" $CWD/commits-hash.sh || exit 1

#cd opencv2fx || exit 1
#make CXX=clang++ BITS=$BITS CONFIG=$CONFIG -j${MKJOBS} || exit 1
#cp -a */$OS-$BITS-*/*.ofx.bundle "$PLUGINDIR" || exit 1
#cd ..


