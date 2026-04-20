# br-thermal — Linux IPP Driver for Brother Thermal Printers

Unofficial open-source Linux driver for Brother thermal printers, implemented
as a [PAPPL](https://www.msweet.org/pappl/) printer application: a
self-contained IPP server that exposes USB-connected Brother printers as
standard network printers, compatible with CUPS, IPP Everywhere, and any
RFC 8011 print client.

The goal is broad coverage of Brother's thermal printer lineup. Currently
supported models are listed below — contributions for additional models are
welcome.

> **Status:** Work in progress — not yet feature-complete. Expect missing features and potential issues.

## Supported Printers

### Brother TD-2000 series (label printers)

| Model | Resolution | Media |
|-------|-----------|-------|
| TD-2020 | 203 dpi | Continuous tape, die-cut labels |
| TD-2120N | 203 dpi | Continuous tape, die-cut labels |
| TD-2125N | 203 dpi | Continuous tape, die-cut labels |
| TD-2125NWB | 203 dpi | Continuous tape, die-cut labels |
| TD-2030A | 300 dpi | Continuous tape, die-cut labels |
| TD-2130N | 300 dpi | Continuous tape, die-cut labels |
| TD-2135N | 300 dpi | Continuous tape, die-cut labels |
| TD-2135NWB | 300 dpi | Continuous tape, die-cut labels |

### Brother PT series (tape printers)

| Model | Resolution | Media |
|-------|-----------|-------|
| PT-E550W | 180 dpi | TZe laminated tape (continuous) |
| PT-P750W | 180 / 360 dpi | TZe laminated tape (continuous) |
| PT-P710BT | 180 dpi | TZe laminated tape (continuous, Bluetooth) |

## Installation (Debian / Ubuntu)

### Build the package

Install the build dependencies, then build a `.deb` for your architecture:

```bash
sudo apt-get install build-essential fakeroot devscripts cmake pkg-config \
  libpappl-dev libcups2-dev libssl-dev libavahi-client-dev libusb-1.0-0-dev

git clone https://github.com/arminfelder/br-thermal-printer-app.git
cd br-thermal-printer-app
fakeroot debian/rules binary

# The .deb is written to the parent directory
ls ../*.deb
```

### Install

```bash
sudo dpkg -i ../br-thermal_*.deb
sudo apt-get install -f   # resolve any missing dependencies

# Enable and start the service
sudo systemctl enable --now br-thermal
sudo systemctl status br-thermal
```

The package:
- installs the binary to `/usr/sbin/br-thermal`
- creates a `_br-thermal` system user and group
- installs a udev rule granting that user USB access to all supported printers
- installs a systemd service that starts automatically on boot

## Configuration

Edit `/etc/default/br-thermal` to configure the service (survives package upgrades):

```bash
# Address the IPP server listens on.
# "localhost" restricts to loopback; use a hostname or IP to expose on the network.
LISTEN_HOSTNAME=localhost

# Log level: fatal | error | warn | info | debug
LOG_LEVEL=info
```

Apply changes by restarting the service:

```bash
sudo systemctl restart br-thermal
journalctl -u br-thermal -f
```

## How It Works

`br-thermal` is a PAPPL printer application — it does **not** use a classic
CUPS filter pipeline. Instead:

- **PAPPL** handles the IPP protocol, web admin UI, DNS-SD/mDNS announcement
  (via Avahi), and USB device I/O
- **br-thermal** provides the driver callbacks that convert PWG raster image
  data into the Brother raster protocol and write it to the USB device

On startup the server scans for connected USB devices and registers each
supported printer as an IPP printer. There is no automatic USB hotplug
detection — if a printer is connected after the service starts, trigger a
rescan via the web UI or by restarting the service.

### Accessing the Web Admin UI

PAPPL includes a built-in web interface for managing printers, checking job
status, and configuring media:

```
http://localhost:8631
```

Replace `localhost` with the server's hostname or IP if `LISTEN_HOSTNAME` is
set to a network address.

### Printing via CUPS

The server announces itself over mDNS so CUPS can discover it automatically.
If auto-discovery is not available, add a queue manually:

```bash
# Example for a TD-2020; replace the printer name in the URI as needed
lpadmin -p brother-td2020 -E \
  -v ipp://localhost:8631/ipp/print/TD-2020 \
  -m everywhere
```

### Spool Directory and State

When installed as a package, job data and printer state are persisted in
`/var/lib/br-thermal` (passed as `-o spool-directory=` in the systemd unit).

When running manually without that flag, PAPPL falls back to a temporary
directory (`$TMPDIR` or `/tmp`) and creates a per-process `pappl<PID>.d`
subdirectory that is removed when the process exits. Use
`-o spool-directory=<path>` to persist state across restarts:

```bash
./build/br-thermal server \
  -o spool-directory=/var/lib/br-thermal \
  -o log-file=- \
  -o log-level=info
```

## Running Without the Package

```bash
# Minimal — listens on localhost only
./build/br-thermal server -o log-file=- -o log-level=debug

# Expose on the network
./build/br-thermal server \
  -o listen-hostname=0.0.0.0 \
  -o server-name=$(hostname -f) \
  -o spool-directory=/var/lib/br-thermal \
  -o log-file=-
```

### USB Permissions

Without the package the service runs as your own user, which typically does
not have USB access to the printer. Options:

```bash
# Option 1: check which group owns the printer's device node and join it
ls -l /dev/bus/usb/$(lsusb -d 04f9: | awk '{print $2"/"substr($4,1,3)}')
sudo usermod -aG <group> $USER   # log out and back in to apply

# Option 2: install the udev rule from the source tree manually
sudo cp dist/60-br-thermal.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb
```

## Building from Source

### Dependencies

- GCC 13+ or Clang 17+ (C++23)
- CMake 3.31+
- `libpappl-dev`, `libcups2-dev`, `libssl-dev`, `libavahi-client-dev`, `libusb-1.0-0-dev`

```bash
# Install build dependencies on Debian/Ubuntu
sudo apt-get install build-essential cmake pkg-config \
  libpappl-dev libcups2-dev libssl-dev libavahi-client-dev libusb-1.0-0-dev
```

### Build

```bash
# Standard release build (uses system PAPPL/CUPS via pkg-config)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Bundle PAPPL and CUPS from source (no system libraries required)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PAPPL_FROM_SOURCE=ON
cmake --build build

# Debug build with AddressSanitizer, UBSan, and LeakSanitizer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build
```

## Tests

```bash
ctest --test-dir build --output-on-failure

# Run individual suites with Catch2 filters
./build/src/drivers/td2000/tests/td2000_tests "[media]"
./build/src/drivers/pte550w/tests/pte550w_tests "[pte550w]"
```

## License

GNU General Public License v3.0 or later. See [LICENSE](LICENSE) for the full text.
