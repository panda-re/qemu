# First run the main Dockerfile to build the base image and name it panda. Then we run here
# to generate a debian package

FROM debian:buster-slim

# Install necessary tools for packaging
RUN apt-get -qq update && \
    DEBIAN_FRONTEND=noninteractive apt-get -qq install -y \
        fakeroot dpkg-dev

# Get dependencies list from base image
COPY --from=panda /tmp/base_dep.txt /tmp
COPY --from=panda /tmp/build_dep.txt /tmp

# Set up /package-root with files from panda we'll package
COPY --from=panda /usr/local/lib/x86_64-linux-gnu /package-root/usr/local/lib/panda
COPY --from=panda /usr/local/share/qemu /package-root/usr/local/share/panda
RUN mkdir -p /package-root/usr/local/lib/panda/contrib/plugins
COPY --from=panda  /panda/build/contrib/plugins/libpanda_plugin_interface.so /package-root/usr/local/lib/panda/contrib/plugins
COPY --from=panda  /usr/include/panda-ng /package-root/usr/include/panda-ng
COPY --from=panda /panda/build/config-host.mak /package-root/usr/local/share/panda
COPY --from=panda /panda/build/qemu-img /package-root/usr/local/bin/qemu-img

# Create DEBIAN directory and control file
COPY control /package-root/DEBIAN/control

# Update control file with dependencies
# Build time. We only select dependencies that are not commented out or blank
RUN dependencies=$(grep '^[a-zA-Z]' /tmp/build_dep.txt | tr '\n' ',' | sed 's/,,\+/,/g'| sed 's/,$//') && \
    sed -i "s/BUILD_DEPENDS_LIST/Build-Depends: $dependencies/" /package-root/DEBIAN/control

# Run time. Also includes ipxe-qemu so we can get pc-bios files
RUN dependencies=$(grep '^[a-zA-Z]' /tmp/base_dep.txt | tr '\n' ',' | sed 's/,,\+/,/g' | sed 's/,$//') && \
    sed -i "s/DEPENDS_LIST/Depends: ipxe-qemu,${dependencies}/" /package-root/DEBIAN/control

# Build the package
RUN fakeroot dpkg-deb --build /package-root /pandare.deb
RUN tar -czvf /libpanda-ng.tar.gz -C /package-root/usr/include/panda-ng .

# The user can now extract the .deb file from the container with something like
#docker run --rm -v $(pwd):/out packager bash -c "cp /pandare.deb /out"
