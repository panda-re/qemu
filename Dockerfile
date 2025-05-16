#syntax=docker/dockerfile:1.17-labs
ARG REGISTRY="docker.io"
ARG BASE_IMAGE="${REGISTRY}/ubuntu:22.04"
ARG TARGET_LIST="x86_64-softmmu,i386-softmmu,arm-softmmu,aarch64-softmmu,ppc-softmmu,ppc64-softmmu,mips-softmmu,mipsel-softmmu,mips64-softmmu,mips64el-softmmu,loongarch64-softmmu,riscv32-softmmu,riscv64-softmmu"

### BASE IMAGE
FROM $BASE_IMAGE AS base

RUN cat > /tmp/base_dep.txt <<EOF
libc6
libgcc-s1
libstdc++6
zlib1g
libglib2.0-0
libgnutls30
libnettle8
libhogweed6
libgmp10
libpixman-1-0
libpng16-16
libjpeg8
libepoxy0
libudev1
libusb-1.0-0
libusbredirparser1
libaio1
libnuma1
libpmem1
libiscsi7
librados2
librbd1
librdmacm1
libibverbs1
libslirp0
libcacard0
libbrlapi0.8
libasound2
libpulse0
libfdt1
libspice-server1
libvirglrenderer1
libdrm2
libgbm1
libx11-6
libx11-xcb1
libxcb-randr0
libssh-4
libcurl3-gnutls
libssl3
libldap-2.5-0
libsasl2-2
libtinfo6
libncursesw6
libgstreamer1.0-0
libgstreamer-plugins-base1.0-0
liborc-0.4-0
libopus0
libvorbis0a
libvorbisenc2
libogg0
libflac8
python3
python3-pip
python3-protobuf
python3-colorama
EOF


# Base image just needs runtime dependencies
RUN apt-get -qq update && \
    DEBIAN_FRONTEND=noninteractive apt-get -qq install -y --no-install-recommends curl $(cat /tmp/base_dep.txt | grep -o '^[^#]*') && \
    apt-get clean

RUN cat > /tmp/build_dep.txt <<EOF
python3-pip
git
libc++-dev
libelf-dev
libtool-bin
libwireshark-dev
libwiretap-dev
lsb-core
zip

# panda build deps
# Note libcapstone-dev is required, but we need v4 + which isn't in apt
build-essential
chrpath
clang-11
gcc
libdwarf-dev
libprotoc-dev
llvm-11-dev
protobuf-c-compiler
protobuf-compiler
python3-dev
libpixman-1-dev
zip

# pypanda dependencies
python3-setuptools
python3-wheel

# pypanda test dependencies
gcc-multilib
libc6-dev-i386
nasm

# Qemu build deps
debhelper
device-tree-compiler
libgnutls28-dev
libaio-dev
libasound2-dev
libattr1-dev
libbrlapi-dev
libcacard-dev
libcap-dev
libcap-ng-dev
libcurl4-gnutls-dev
libdrm-dev
libepoxy-dev
libfdt-dev
libgbm-dev
libibumad-dev
libibverbs-dev
libiscsi-dev
libjpeg-dev
libncursesw5-dev
libnuma-dev
libpmem-dev
libpng-dev
libpulse-dev
librbd-dev
librdmacm-dev
libsasl2-dev
libseccomp-dev
libslirp-dev
libspice-protocol-dev
libspice-server-dev
libssh-dev
libudev-dev
libusb-1.0-0-dev
libusbredirparser-dev
libvirglrenderer-dev
nettle-dev
python3
python3-sphinx
texinfo
uuid-dev
xfslibs-dev
zlib1g-dev
libc6.1-dev-alpha-cross

# qemu build deps that conflict with gcc-multilib
#gcc-alpha-linux-gnu
#gcc-powerpc64-linux-gnu
#gcc-s390x-linux-gnu

# rust install deps
curl

# libosi install deps
cmake
ninja-build
rapidjson-dev 
EOF

### BUILD IMAGE - STAGE 2
FROM base AS builder

RUN apt-get -qq update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $(cat /tmp/build_dep.txt | grep -o '^[^#]*') && \
    apt-get clean

# Build and install panda
# Copy repo root directory to /panda, note we explicitly copy in .git directory
# Note .dockerignore file keeps us from copying things we don't need
COPY --exclude=.git \
    --exclude=.github \
    --exclude=.gitlab-ci.d \
    --exclude=Dockerfile \
    --exclude=panda/debian \
    . /panda/

ARG TARGET_LIST
# Note we diable NUMA for docker builds because it causes make check to fail in docker
RUN mkdir /panda/build && cd /panda/build && \
     python3 -m pip install setuptools_scm && \
     /panda/configure \
         --target-list="${TARGET_LIST}" \
         --enable-plugins


RUN ninja -C /panda/build -j "$(nproc)"

FROM builder AS installer
RUN  ninja -C /panda/build install

FROM builder AS libgen
RUN apt-get install -y gdb && \
    python3 -m pip install cffi tree-sitter==0.24.0 tree-sitter-c==0.23.0

RUN git clone https://github.com/panda-re/libpanda-ng /libpanda-ng && \
    mkdir /libpanda-ng/build && cd /libpanda-ng/build && \
    bash /libpanda-ng/run_all.sh /panda

# this layer is used to strip shared objects and change python data to be
# symlinks to the installed panda data directory
FROM installer AS cleanup
RUN find /usr/local/lib/x86_64-linux-gnu -name "*.so" -exec strip {} \;
RUN strip /panda/build/contrib/plugins/libpanda_plugin_interface.so
RUN mkdir -p /usr/include/panda-ng
COPY --from=libgen /libpanda-ng/build/* /usr/include/panda-ng

FROM base AS packager

# Install necessary tools for packaging
RUN apt-get -qq update && \
    DEBIAN_FRONTEND=noninteractive apt-get -qq install -y \
        fakeroot dpkg-dev

# Set up /package-root with files from panda we'll package
COPY --from=cleanup /usr/local/lib/x86_64-linux-gnu /package-root/usr/local/lib/panda
COPY --from=cleanup /usr/local/share/qemu /package-root/usr/local/share/panda
RUN mkdir -p /package-root/usr/local/lib/panda/contrib/plugins /package-root/DEBIAN/
COPY --from=cleanup  /panda/build/contrib/plugins/libpanda_plugin_interface.so /package-root/usr/local/lib/panda/contrib/plugins
COPY --from=cleanup  /usr/include/panda-ng /package-root/usr/include/panda-ng
COPY --from=cleanup /panda/build/config-host.mak /package-root/usr/local/share/panda
COPY --from=cleanup /panda/build/qemu-img /package-root/usr/local/bin/qemu-img

# Create DEBIAN directory and control file
COPY ./panda/debian/control /package-root/DEBIAN/control

# Update control file with dependencies
# Build time. We only select dependencies that are not commented out or blank
RUN dependencies=$(grep '^[a-zA-Z]' /tmp/build_dep.txt | tr '\n' ',' | sed 's/,,\+/,/g'| sed 's/,$//') && \
    sed -i "s/BUILD_DEPENDS_LIST/Build-Depends: $dependencies/" /package-root/DEBIAN/control

# Run time. Also includes ipxe-qemu so we can get pc-bios files
RUN dependencies=$(grep '^[a-zA-Z]' /tmp/base_dep.txt | tr '\n' ',' | sed 's/,,\+/,/g' | sed 's/,$//') && \
    sed -i "s/DEPENDS_LIST/Depends: ${dependencies}/" /package-root/DEBIAN/control

# Build the package
RUN fakeroot dpkg-deb --build /package-root /pandare.deb
RUN tar -czvf /libpanda-ng.tar.gz -C /package-root/usr/include/panda-ng .

FROM base AS panda
COPY --from=cleanup /panda/build/libpanda* /usr/local/bin
COPY --from=cleanup /usr/include/panda-ng /usr/include/panda-ng