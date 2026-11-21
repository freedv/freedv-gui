# Building FreeDV GUI

This document describes how to build the FreeDV GUI program for various operating systems.  FreeDV GUI is developed on Ubuntu Linux, and then cross compiled for Windows using Fedora Linux (Fedora has great cross compiling support) and Docker.

## Further Reading

  * http://freedv.org - introduction, documentation, downloads
  * [FreeDV GUI User Manual](USER_MANUAL.md)
  * Information on third party code included in FreeDV [here](./src/3rdparty/README.md)
  * [Code signing information for FreeDV](./CODE_SIGNING.md)

## Installing prerequisites on Ubuntu Linux

  ```
  $ sudo apt install libspeexdsp-dev sox git \
  libwxgtk3.2-dev libhamlib-dev libasound2-dev libao-dev \
  libgsm1-dev libsndfile1-dev cmake module-assistant build-essential \
  autoconf automake libtool libebur128-dev gettext libssl-dev
  $ git clone https://github.com/freedv/freedv-gui.git
  $ cd freedv-gui

  (if using pipewire/PulseAudio -- recommended and the default) 
  $ sudo apt install libpulse-dev pulseaudio-utils
  
  (if using PortAudio)
  $ sudo apt install portaudio19-dev
  ```

  (Depending on release you may need to use `libwxgtk3.0-gtk3-dev` instead of `libwxgtk3.2-dev`.)
  
## Installing prerequisites on Fedora Linux

  ```
  $ sudo dnf groupinstall "Development Tools"
  $ sudo dnf install cmake wxGTK3-devel \
    libsndfile-devel speexdsp-devel hamlib-devel alsa-lib-devel libao-devel \
    gsm-devel gcc-c++ sox autoconf automake libtool libebur128-devel gettext openssl-devel
  $ git clone https://github.com/freedv/freedv-gui.git
  $ cd freedv-gui

  (if using pipewire/PulseAudio -- default and recommended)
  $ sudo dnf install pulseaudio-libs-devel pulseaudio-utils

  (if using PortAudio)
  $ sudo dnf install portaudio-devel
  ```

## Running FreeDV on Linux

1. Build FreeDV to make sure the correct dependencies are linked in:

   ```
   (rade-venv) $ pwd
   /home/<user>/freedv-gui
   (rade-venv) $ ./build_linux.sh
   ```

2. Execute FreeDV:

   ```
   (rade-venv) $ pwd
   /home/<user>/freedv-gui/build_linux
   (rade-venv) $ export GDK_BACKEND=x11 # optional, see (*) below
   (rade-venv) $ src/freedv
   ```

(*) If your Linux distribution and/or desktop environment uses Wayland, FreeDV will always open in the middle 
of the screen, regardless of where you positioned it before. You can avoid this by having FreeDV run as an 
X11 application instead using XWayland (`GDK_BACKEND=x11`).

Alternatively, you can use [this script](https://github.com/barjac/freedv-rade-build) developed by 
Barry Jackson G4MKT to automate the above steps. While the FreeDV project thanks him for his contribution
to helping Linux users more easily get on the air with FreeDV, the FreeDV development team will not provide 
support. All support inquiries regarding this script should be directed to the linked repo.

## Audio driver selection

By default, FreeDV uses the native audio APIs on certain platforms. These are as follows:

| Platform | Audio API |
|---|---|
| macOS | Core Audio |
| Linux | pipewire (via PulseAudio library) |
| Windows | WASAPI |

On platforms not listed above, PortAudio is used instead. PortAudio can also be explicitly selected by the
user by defining the environment variable `USE_NATIVE_AUDIO=0` before running the `build_*.sh` script
(or specifying `-DUSE_NATIVE_AUDIO=0` to `cmake`).

## Installing on Linux

You need to install the codec2 shared libraries, and freedv-gui:
  ```
  $ cd ~/freedv-gui/codec2/build_linux
  $ sudo make install
  $ cd ~/freedv-gui/build_linux
  $ sudo make install
  $ sudo ldconfig
  ```

## Radio integrations

FreeDV supports direct integration with several different types of radios. Please see [this README](./src/integrations/README.md) for
more information.
 
## Testing

The `wav` directory contains test files of modulated audio that you can use to test FreeDV (see the [USER_MANUAL](USER_MANUAL.md)).

## Building for Windows

Windows releases are built using the LLVM version of MinGW. This allows
one to build FreeDV for ARM as well as for Intel Windows systems.

### Prerequisites

* CMake >= 3.25.0
* Linux (tested on Ubuntu 22.04)
    * *NOTE: This does not currently work on macOS due to CMake using incorrect library suffixes.*
* NSIS for generating the installer (for example, `sudo apt install nsis` on Ubuntu)
* gettext to include translations

### Instructions

1. Download LLVM MinGW at https://github.com/mstorsjo/llvm-mingw/releases/.
2. Decompress into your preferred location. For example: `tar xvf llvm-mingw-20220906-ucrt-ubuntu-18.04-x86_64.tar.xz` (The exact filename here will depend on the file downloaded in step (1). Note that for best results, you should use a build containing "ucrt" in the file name corresponding to the platform which you're building the Windows binary from.)
3. Add LLVM MinGW to your PATH: `export PATH=/path/to/llvm-mingw-20220906-ucrt-ubuntu-18.04-x86_64/bin:$PATH`. (The folder containing the LLVM tools is typically named the same as the file downloaded in step (2) minus the extension.)
4. Create a build folder inside freedv-gui: `mkdir build_windows`
5. Run CMake to configure the FreeDV build: `cd build_windows && cmake -DCMAKE_TOOLCHAIN_FILE=${PWD}/../cross-compile/freedv-mingw-llvm-[architecture].cmake ..`
   * Valid architectures are: aarch64 (64 bit ARM), x86_64 (64 bit Intel/AMD)
6. Build FreeDV as normal: `make` (You can also add `-j[num]` to the end of this command to use multiple cores and shorten the build time.)
7. Create FreeDV installer: `make package`

## Building and installing on macOS

Using MacPorts, most of the appropriate dependencies can be installed by:

```
$ sudo port install automake git libtool sox +universal cmake wget pkgconf gettext
```

and on Homebrew:

```
$ brew install automake libtool git sox cmake wget pkgconf gettext
```

Once the dependencies are installed, you can then run the `build_osx.sh` script inside the source tree to build
FreeDV and associated libraries (codec2, hamlib). A FreeDV.app app bundle will be created inside the build_osx/src
folder which can be copied to your system's Applications folder.

*Note: for distribution, code signing is required. The following commands can be run to enable this:*

```
CODESIGN_IDENTITY=[identity in your keychain] UNIV_BUILD=1 ./build_osx.sh
cd build_osx
make release
```

## Building with Profile Guided Optimization (PGO)

Profile Guided Optimization is an optimization strategy supported by the Clang compiler that uses profiling data
to govern optimization decisions. This can significantly improve the runtime performance of many applications, especially
with higher optimization levels. In testing with FreeDV, the AppImages are able to use up to 25% less CPU using a
combination of `BUILD_TYPE=Release`, link-time optimization (LTO) and PGO. Currently this is supported for macOS and Linux 
using the LLVM version of the Clang compiler (*not* the one Apple ships with Xcode).

To build with PGO enabled:

1. Perform an initial instrumented build using the `./build_linux.sh` (or `./build_osx.sh`) script, making sure to overide
   the relevant environment variables to use the correct version of Clang. Example below with macOS and Homebrew:

```
$ BUILD_TYPE=Release UT_ENABLE=0 BUILD_DEPS=1 PGO_INSTRUMENT=1 CC=$(brew --prefix llvm@20)/bin/clang CXX=$(brew --prefix llvm@20)/bin/clang++ OBJCXX=$(brew --prefix llvm@20)/bin/clang ./build_osx.sh 
```

2. Execute FreeDV and use normally to obtain sufficient profiling data. An automated script is available to do this:

```
cd build_osx
LLVM_PROFILE_FILE="code-%p.profraw" FREEDV_COMPUTER_TO_RADIO_DEVICE="VB-Cable" FREEDV_RADIO_TO_COMPUTER_DEVICE="VB-Cable" FREEDV_COMPUTER_TO_SPEAKER_DEVICE="BlackHole1 2ch" FREEDV_MICROPHONE_TO_COMPUTER_DEVICE="BlackHole2 2ch" ../test/generate_pgo_profiles.sh 2>&1
$(brew --prefix llvm@20)/bin/llvm-profdata merge -output $(pwd)/../code.profdata code-*.profraw
```

(`FREEDV_COMPUTER_TO_RADIO_DEVICE`, `FREEDV_RADIO_TO_COMPUTER_DEVICE`, `FREEDV_COMPUTER_TO_SPEAKER_DEVICE` and `FREEDV_MICROPHONE_TO_COMPUTER_DEVICE` are optional on Linux,
where PulseAudio/pipewire null sink(s) will be created if these are not provided).

3. Perform a final build with the collected profiling data:

```
rm -rf build_osx
BUILD_TYPE=Release UT_ENABLE=0 UNIV_BUILD=1 BUILD_DEPS=1 PGO_USE_PROFILE=$(pwd)/code.profdata CC=$(brew --prefix llvm@20)/bin/clang CXX=$(brew --prefix llvm@20)/bin/clang++ OBJCXX=$(brew --prefix llvm@20)/bin/clang  ./build_osx.sh
```

Limitations:

1. The best results are obtained if having FreeDV build all required dependencies itself (`BUILD_DEPS=1`). The optimization process 
   is unable to touch already-compiled dyanmic libraries, so performance gains will likely be less without this option.
