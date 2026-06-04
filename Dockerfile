FROM debian:trixie

ARG HOST=aarch64-linux-gnu
ARG ARCH=arm64

# Build profiles passed to dpkg-buildpackage. Default = static: CUPS/PAPPL built
# from source (BUILD_PAPPL_FROM_SOURCE). For a dynamic build pass
# PROFILES=cross,nocheck and supply the distro -dev packages via EXTRA_BUILD_DEPS
# (libpappl-dev:<arch> libcups2-dev:<arch>).
ARG PROFILES=cross,nocheck,static
ARG EXTRA_BUILD_DEPS=

RUN dpkg --add-architecture ${ARCH}

RUN apt-get update

# Toolchain + the :${ARCH} -dev packages that CUPS/PAPPL link against. CUPS and
# PAPPL themselves are built statically from source by the `static` build
# profile (BUILD_PAPPL_FROM_SOURCE in debian/rules), so no libpappl-dev /
# libcups2-dev here. git is needed for the FetchContent clone of CUPS/PAPPL.
RUN apt-get install -y --no-install-recommends \
      build-essential \
      crossbuild-essential-${ARCH} \
      debhelper \
      cmake \
      pkg-config \
      git \
      ca-certificates \
      autoconf \
      libssl-dev:${ARCH} \
      libavahi-client-dev:${ARCH} \
      libusb-1.0-0-dev:${ARCH} \
      zlib1g-dev:${ARCH} \
      libpng-dev:${ARCH} \
      libjpeg-dev:${ARCH} \
      ${EXTRA_BUILD_DEPS}

COPY . /src

WORKDIR /src

# `static` profile => STATIC_LIBSTDCXX=ON + BUILD_PAPPL_FROM_SOURCE=ON
# (debian/rules), yielding a binary with libstdc++, CUPS and PAPPL all linked
# statically; only their system deps (ssl, avahi, usb, z, png, jpeg) stay dynamic.
RUN CONFIG_SITE=/etc/dpkg-cross/cross-config.${ARCH} \
      dpkg-buildpackage --host-arch ${ARCH} -P${PROFILES} -us -uc -b

# Copy .deb packages to /output so callers can bind-mount a host directory there.
# Example: docker run --rm -v $(pwd)/dist:/output <image>
RUN mkdir -p /output && cp /*.deb /output/
