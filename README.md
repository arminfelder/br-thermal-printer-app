# br-thermal

Unofficial PAPPL-based IPP printer driver for Brother thermal printers.

> **Status:** Work in progress — not yet feature-complete. Expect missing features and potential issues.

## Supported Devices

### TD-2000 series (label printers)

| Model | DPI | Media |
|-------|-----|-------|
| TD-2020 | 203 | Continuous tape, die-cut labels |
| TD-2120N | 203 | Continuous tape, die-cut labels |
| TD-2125N | 203 | Continuous tape, die-cut labels |
| TD-2125NWB | 203 | Continuous tape, die-cut labels |
| TD-2030A | 300 | Continuous tape, die-cut labels |
| TD-2130N | 300 | Continuous tape, die-cut labels |
| TD-2135N | 300 | Continuous tape, die-cut labels |
| TD-2135NWB | 300 | Continuous tape, die-cut labels |

### PT-E550W / PT-P750W / PT-P710BT (tape printers)

| Model | DPI | Media |
|-------|-----|-------|
| PT-E550W | 180 | TZe laminated tape (continuous) |
| PT-P750W | 180 / 360 | TZe laminated tape (continuous) |
| PT-P710BT | 180 | TZe laminated tape (continuous, Bluetooth) |

## Dependencies

- [PAPPL](https://www.msweet.org/pappl/) 1.4+
- [CUPS](https://www.openprinting.org/software/cups/) 2.4+
- CMake 3.20+
- C++23 capable compiler (GCC 13+ or Clang 17+)

## Build

```bash
# Using system PAPPL/CUPS (via pkg-config)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Build PAPPL and CUPS from source (fetches CUPS 2.4.16 + PAPPL 1.4.10)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PAPPL_FROM_SOURCE=ON
cmake --build build

# Debug build with AddressSanitizer / UBSan / LeakSanitizer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build
```

## Run

```bash
./build/br-thermal
```

The server starts an IPP printer application that auto-discovers connected Brother printers via USB and exposes them as network printers.

## Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run individual test suites (supports Catch2 tag/name filters)
./build/src/drivers/td2000/tests/td2000_tests "[media]"
./build/src/drivers/pte550w/tests/pte550w_tests "[pte550w]"
```

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

See [LICENSE](LICENSE) for details.