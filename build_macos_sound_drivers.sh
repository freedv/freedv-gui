#!/bin/bash

# Stop on the first failure so a driver that did not build is not silently
# skipped (the tests would then run without their BlackHole devices).
set -e

git clone https://github.com/freedv/BlackHole.git
cd BlackHole

# How many (BlackHole1, BlackHole2, ...): two unless told otherwise. The GUI
# tests (test/gui) need four, each a separate loopback device, for the
# radio's input and output, the microphone and the speaker: two devices of
# the same driver ("BlackHole1 2ch" and "BlackHole1 2ch 2") share what's
# played into them.
DRIVERS=${1:-2}

for i in $(seq 1 $DRIVERS); do
    git reset --hard
    rm -rf build

    export bundleID=audio.existential.BlackHole$i
    export driverName=BlackHole$i

    xcodebuild \
        -project BlackHole.xcodeproj \
        -configuration Release \
        -target BlackHole \
        CONFIGURATION_BUILD_DIR=build \
        PRODUCT_BUNDLE_IDENTIFIER=$bundleID \
        GCC_PREPROCESSOR_DEFINITIONS="$GCC_PREPROCESSOR_DEFINITIONS \
            kNumber_Of_Channels='2' \
            kPlugIn_BundleID='\"$bundleID\"' \
            kDriver_Name='\"$driverName\"' \
            kDevice2_IsHidden=false \
            kDevice2_HasInput=true \
            kDevice2_HasOutput=true" \
            kLatency_Frame_Size='128' \
        MACOSX_DEPLOYMENT_TARGET=12.0

    sudo mv build/BlackHole.driver /Library/Audio/Plug-Ins/HAL/$driverName.driver
done

sudo killall -9 coreaudiod
