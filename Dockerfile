ARG BASE_IMAGE="ubuntu:22.04"
ARG TARGET_LIST="x86_64-softmmu,i386-softmmu,arm-softmmu,aarch64-softmmu,ppc-softmmu,ppc64-softmmu,mips-softmmu,mipsel-softmmu,mips64-softmmu,mips64el-softmmu,loongarch64-softmmu,riscv32-softmmu,riscv64-softmmu"

### BASE IMAGE
FROM $BASE_IMAGE AS base
ARG BASE_IMAGE

# Copy dependencies lists into container. We copy them all and then do a mv because
# we need to transform base_image into a windows compatible filename which we can't
# do in a COPY command.
COPY ./panda/dependencies/* /tmp
RUN mv /tmp/$(echo "$BASE_IMAGE" | sed 's/:/_/g')_build.txt /tmp/build_dep.txt && \
    mv /tmp/$(echo "$BASE_IMAGE" | sed 's/:/_/g')_base.txt /tmp/base_dep.txt

# Base image just needs runtime dependencies
RUN [ -e /tmp/base_dep.txt ] && \
    apt-get -qq update && \
    DEBIAN_FRONTEND=noninteractive apt-get -qq install -y --no-install-recommends curl $(cat /tmp/base_dep.txt | grep -o '^[^#]*') && \
    apt-get clean

### BUILD IMAGE - STAGE 2
FROM base AS builder
ARG BASE_IMAGE
ARG TARGET_LIST

RUN [ -e /tmp/build_dep.txt ] && \
    apt-get -qq update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $(cat /tmp/build_dep.txt | grep -o '^[^#]*') && \
    apt-get clean

# Build and install panda
# Copy repo root directory to /panda, note we explicitly copy in .git directory
# Note .dockerignore file keeps us from copying things we don't need
COPY . /panda/

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

FROM base AS panda
COPY --from=cleanup /panda/build/libpanda* /usr/local/bin
COPY --from=cleanup /usr/include/panda-ng /usr/include/panda-ng