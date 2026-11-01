#!/usr/bin/env bash
# Minimal native MinGW media runtime. Run from Git Bash; no MSVC DLLs required.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
prefix="$root/build/media-sdk"
export PATH="$(cygpath -u "$AIRCONNECT_GCC_DIR"):$(cygpath -u "$AIRCONNECT_NASM_DIR"):$root/build/media-tools/usr/bin:$root/build/media-tools:$PATH"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig"
mkdir -p "$prefix/include/ffnvcodec" "$prefix/lib/pkgconfig"
cp build/media-src/nv-codec-headers/include/ffnvcodec/*.h "$prefix/include/ffnvcodec/"
sed "s|@@PREFIX@@|$(cygpath -m "$prefix")|g" build/media-src/nv-codec-headers/ffnvcodec.pc.in > "$prefix/lib/pkgconfig/ffnvcodec.pc"
export GIT_CEILING_DIRECTORIES="$root/build/media-src"
if [[ ! -f "$prefix/lib/libx264.a" ]]; then
cd "$root/build/media-src/x264"
./configure --host=x86_64-w64-mingw32 --prefix="$(cygpath -m "$prefix")" --enable-static --disable-cli --disable-opencl --enable-win32thread --extra-cflags=-O2 --extra-ldflags=-static-libgcc
make -j4
make install
fi
cd "$root/build/media-src/ffmpeg"
printf '7.1.1\n' > VERSION
./configure --prefix="$(cygpath -m "$prefix")" --arch=x86_64 --target-os=mingw32 --cc=gcc --cxx=g++ \
 --enable-shared --disable-static --disable-programs --disable-doc --disable-debug \
 --disable-everything --disable-autodetect --disable-avfilter --disable-avdevice --disable-postproc --enable-gpl --enable-version3 --enable-w32threads \
 --enable-libx264 --enable-ffnvcodec --enable-nvenc --enable-d3d11va --enable-dxva2 --enable-schannel \
 --enable-decoder=h264,hevc,aac,alac,pcm_s16le,pcm_s16be,mp3 \
 --enable-encoder=libx264,h264_nvenc,hevc_nvenc,aac,alac,pcm_s16le \
 --enable-parser=h264,hevc,aac,mpegaudio --enable-hwaccel=h264_d3d11va,h264_d3d11va2,hevc_d3d11va,hevc_d3d11va2 \
 --enable-demuxer=mov,mpegts,hls,aac,mp3 --enable-muxer=mp4,mov,adts \
 --enable-protocol=file,http,https,tcp,tls,crypto --enable-network \
 --enable-swscale --enable-swresample \
 --extra-ldflags=-static-libgcc --pkg-config=pkg-config
make -j4
make install
printf '\nNative media dependencies built in %s\n' "$prefix"

