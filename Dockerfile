FROM ubuntu:24.04 AS builder

ENV CORE_ASSETS https://github.com/CollaboraOnline/online/releases/download/for-code-assets/core-co-24.04-assets.tar.gz
ENV BUILDDIR /src
ENV ONLINE_EXTRA_BUILD_OPTIONS --enable-experimental

WORKDIR /src

RUN --mount=type=cache,target=/var/cache/apt apt-get update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get -y install libpng16-16 fontconfig adduser cpio tzdata \
    findutils nano \
    libcap2-bin openssl openssh-client \
    libxcb-shm0 libxcb-render0 libxrender1 libxext6 \
    fonts-wqy-zenhei fonts-wqy-microhei fonts-droid-fallback \
    fonts-noto-cjk \
    libpoco-dev python3-polib libcap-dev npm \
    libpam-dev libzstd-dev wget git build-essential libtool \
    libcap2-bin python3-lxml libpng-dev libcppunit-dev \
    pkg-config fontconfig snapd chromium-browser \
    rsync curl \
    # core build dependencies, only those that are needed for LOKit
    git build-essential zip ccache autoconf gperf nasm xsltproc flex bison

RUN curl -fsSL https://deb.nodesource.com/setup_20.x -o nodesource_setup.sh && bash nodesource_setup.sh && apt install -y nodejs

RUN mkdir -p $BUILDDIR

# Build poco separately to cache it
ADD https://pocoproject.org/releases/poco-1.12.5p2/poco-1.12.5p2-all.tar.gz /src/builddir/poco-1.12.5p2-all.tar.gz

RUN cd builddir && tar xf poco-1.12.5p2-all.tar.gz && cd poco-1.12.5p2-all/ && \
    ./configure --static --no-tests --no-samples --no-sharedlibs --cflags="-fPIC" --omit=Zip,Data,Data/SQLite,Data/ODBC,Data/MySQL,MongoDB,PDF,CppParser,PageCompiler,Redis,Encodings,ActiveRecord --prefix=$BUILDDIR/poco && \
    make -j $(nproc) && \
    make install

RUN mkdir /src/builddir/core && \
    wget "$CORE_ASSETS" -O /src/builddir/core/core-assets.tar.xz

COPY . /src/builddir/online

RUN cp /src/builddir/online/docker/from-base/base/build.sh ./

RUN bash build.sh

FROM ubuntu:24.04 AS final
COPY --from=builder /src/instdir /app/instdir
