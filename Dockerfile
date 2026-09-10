# syntax=docker/dockerfile:1.7

FROM archlinux:base-devel AS deps

ARG OPENCV_VERSION=4.14.0
ARG LEPTONICA_VERSION=1.87.0
ARG TESSERACT_VERSION=5.5.3

ENV PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

RUN --mount=type=cache,target=/var/cache/pacman/pkg,sharing=locked \
    pacman -Sy --noconfirm --needed archlinux-keyring && \
    pacman -Su --noconfirm --needed \
        cmake \
        ninja \
        git \
        pkgconf \
        curl \
        mesa \
        libglvnd \
        libx11 \
        libxext \
        libxrandr \
        libxinerama \
        libxcursor \
        libxi \
        wayland \
        wayland-protocols \
        libxkbcommon \
        libpipewire \
        dbus

RUN git clone --depth 1 --branch "${OPENCV_VERSION}" https://github.com/opencv/opencv.git /tmp/opencv && \
    cmake -S /tmp/opencv -B /tmp/opencv/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_LIST=core,imgproc,imgcodecs \
        -DBUILD_TESTS=OFF \
        -DBUILD_PERF_TESTS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_DOCS=OFF \
        -DBUILD_JAVA=OFF \
        -DBUILD_opencv_apps=OFF \
        -DBUILD_ZLIB=ON \
        -DBUILD_PNG=ON \
        -DBUILD_JPEG=ON \
        -DWITH_TIFF=OFF \
        -DWITH_WEBP=OFF \
        -DWITH_OPENJPEG=OFF \
        -DWITH_JASPER=OFF \
        -DWITH_OPENEXR=OFF \
        -DWITH_QT=OFF \
        -DWITH_GTK=OFF \
        -DWITH_VTK=OFF \
        -DWITH_FFMPEG=OFF \
        -DWITH_GSTREAMER=OFF \
        -DWITH_V4L=OFF \
        -DWITH_IPP=OFF \
        -DWITH_PROTOBUF=OFF \
        -DWITH_ADE=OFF && \
    cmake --build /tmp/opencv/build --parallel && \
    cmake --install /tmp/opencv/build && \
    rm -rf /tmp/opencv

RUN git clone --depth 1 --branch "${LEPTONICA_VERSION}" https://github.com/DanBloomberg/leptonica.git /tmp/leptonica && \
    cmake -S /tmp/leptonica -B /tmp/leptonica/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_PROG=OFF \
        -DSW_BUILD=OFF \
        -DENABLE_ZLIB=OFF \
        -DENABLE_PNG=OFF \
        -DENABLE_JPEG=OFF \
        -DENABLE_TIFF=OFF \
        -DENABLE_GIF=OFF \
        -DENABLE_WEBP=OFF \
        -DENABLE_OPENJPEG=OFF && \
    cmake --build /tmp/leptonica/build --parallel && \
    cmake --install /tmp/leptonica/build && \
    rm -rf /tmp/leptonica

RUN git clone --depth 1 --branch "${TESSERACT_VERSION}" https://github.com/tesseract-ocr/tesseract.git /tmp/tesseract && \
    cmake -S /tmp/tesseract -B /tmp/tesseract/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TRAINING_TOOLS=OFF \
        -DBUILD_TESTS=OFF \
        -DDISABLED_LEGACY_ENGINE=ON \
        -DDISABLE_ARCHIVE=ON \
        -DDISABLE_CURL=ON \
        -DDISABLE_TIFF=ON \
        -DOPENMP_BUILD=OFF \
        -DSW_BUILD=OFF && \
    cmake --build /tmp/tesseract/build --parallel && \
    cmake --install /tmp/tesseract/build && \
    rm -rf /tmp/tesseract

FROM deps AS builder

ARG RUNEHELPER_LINUX_BACKEND=wayland

WORKDIR /src
COPY . .

RUN --mount=type=cache,target=/build,sharing=locked \
    cmake -S /src -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DRUNEHELPER_LINUX_BACKEND="${RUNEHELPER_LINUX_BACKEND}" \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" && \
    cmake --build /build --parallel && \
    install -Dm755 -s /build/RuneHelper /out/RuneHelper

FROM scratch AS export

COPY --from=builder /out/RuneHelper /RuneHelper
