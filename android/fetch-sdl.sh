#!/bin/sh
# Downloads SDL 2 and puts what the app needs in place (both are git-ignored):
#   android/SDL                                  SDL's source, built by app/CMakeLists.txt
#   android/app/src/main/java/org/libsdl/app     SDL's Java side, which SDLActivity lives in
set -e
cd "$(dirname "$0")"
VER=2.30.9
if [ ! -d SDL ]; then
    curl -sSL "https://github.com/libsdl-org/SDL/releases/download/release-$VER/SDL2-$VER.tar.gz" | tar xz
    mv "SDL2-$VER" SDL
fi
mkdir -p app/src/main/java/org/libsdl
rm -rf app/src/main/java/org/libsdl/app
cp -r SDL/android-project/app/src/main/java/org/libsdl/app app/src/main/java/org/libsdl/app
