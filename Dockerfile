FROM debian:trixie

RUN dpkg --add-architecture arm64

RUN apt-get update

RUN apt-get install -y --no-install-recommends \
      build-essential \
      crossbuild-essential-arm64 \
      debhelper \
      cmake \
      pkg-config \
      libpappl-dev:arm64 \
      libcups2-dev:arm64 \
      libssl-dev:arm64 \
      libavahi-client-dev:arm64 \
      libusb-1.0-0-dev:arm64

# Bookworm's libcups2-dev (2.4.2) predates cups.pc; generate it for the arm64 sysroot
RUN CUPS_VER=$(dpkg-query -W -f='${Version}' libcups2-dev:arm64 | cut -d- -f1) && \
    mkdir -p /usr/lib/aarch64-linux-gnu/pkgconfig && \
    printf 'prefix=/usr\nexec_prefix=${prefix}\nlibdir=${prefix}/lib/aarch64-linux-gnu\nincludedir=${prefix}/include\n\nName: CUPS\nDescription: CUPS API Library\nVersion: %s\nCflags: -I${includedir}\nLibs: -L${libdir} -lcups\n' "$CUPS_VER" \
      > /usr/lib/aarch64-linux-gnu/pkgconfig/cups.pc

COPY . /src

WORKDIR /src

RUN ./build-deb-arm64.sh

# Copy .deb packages to /output so callers can bind-mount a host directory there.
# Example: docker run --rm -v $(pwd)/dist:/output <image>
RUN mkdir -p /output && cp /*.deb /output/
