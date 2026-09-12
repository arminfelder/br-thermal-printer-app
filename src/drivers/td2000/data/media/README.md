# Media Info

Each `.bin` is the 127-byte payload of the `ESC i U` command (`1Bh 69h 55h 77h 01h`).

## Provenance

The blobs come from the Brother PTD source files. Each file is
the payload extracted from the matching Brother command stream, which has this layout:

| Offset | Bytes | Content |
|--------|-------|---------|
| 0x00   | 4     | `1B 69 61 01` — select raster mode |
| 0x04   | 16    | `1B 69 55 4F …` — model identification |
| 0x14   | 5     | `1B 69 55 77 01` — media information header |
| 0x19   | 127   | **the payload stored here** |
| 0x98   | 4     | `1B 69 61 FF` |

Verified on 2026-09-12: all 18 blobs are byte-identical to the payload of the matching
Brother stream. Each blob that has a `[PaperNN]` block agrees with it on 19 fields
(`nSensorID`, `byEnergyRank`, `nPaperWidth/10`, `nPaperLength/10`, `byHeadDivide`,
`byRollWidMm`, `wPinOffsetLeft`, `nImageAreaWidthRes`, `nImageAreaLengthRes`,
`nDieStartPlus`, `nDieStartRevPlus`, `wDieStartFwdPlus`, `nVirtualOffsetX`,
`nVirtualOffsetY`, `nAfterFeedPlus`, `wPafMediaID`, `lblPitchDot`, `szSizeIN`, and all 12
values of `reserved_12_`). The td2x2x blobs use `bst202ed.txt` (TD-2020, 203 dpi) and the
td2x3x blobs use `bst213ed.txt` (TD-2130N, 300 dpi).

Many of those fields are 0 in every medium, so a match does not prove their offset. Offsets
[12] to [14] are 0 everywhere and stay unproven. The header of `MediaInfoFields` records
which offsets the data pins and which it does not.

### Record count

Brother also ships a complete paper table: 5 header bytes and then one 127-byte record per
medium (18 records for the TD-2020, 8 for the TD-2130N). Its header is `1B 69 55 77 12` and
`1B 69 55 77 08`. The fifth byte is therefore the number of records that follow, not a
constant `01`. This driver sends one record, so it uses `01`.

`51x30mm.bin` is not present. Brother ships a file with that name, but its payload is the
30x30 mm record (`wPafMediaID` 431, `szSizeMM` "30mm x 30mm"). `bst202ed.txt` defines no
51x30 mm paper, and the label table of raster spec 2.3.2 (b) does not list one.

`51x26mm.bin` has no `[PaperNN]` block in `bst202ed.txt`, but `nDefaultPaperSize=422` in the
`[Model]` block is its `wPafMediaID`, so it is the default medium of the TD-2020.

`CHECKSUMS.md5` records the expected content of each file. The paths in it are relative, so
run the check from this directory:

```
cd src/drivers/td2000/data/media && md5sum -c CHECKSUMS.md5
```

## Conformance to the Raster Command Reference

Section 2.3.2 (b) of the Raster Command Reference gives the geometry of each die-cut label.
The blobs agree with it:

| ID  | Label       | Print area (dots, 203 dpi) | Print area (dots, 300 dpi) |
|-----|-------------|----------------------------|----------------------------|
| 422 | 51 x 26 mm  | 382 x 157                  | 564 x 231                  |
| 431 | 30 x 30 mm  | 216 x 192                  | 318 x 283                  |
| 432 | 40 x 40 mm  | 296 x 272                  | 436 x 401                  |
| 433 | 40 x 50 mm  | 296 x 352                  | 436 x 519                  |
| 434 | 40 x 60 mm  | 296 x 432                  | 436 x 638                  |
| 435 | 50 x 30 mm  | 376 x 192                  | (not tabulated)            |
| 437 | 60 x 60 mm  | 448 x 432                  | (not tabulated)            |

These seven IDs are the complete die-cut set of the document. With `57mm` and `58mm` they
make the nine blobs of each family. The "Length offset" column of the same table (24 dots at
203 dpi, 35 at 300 dpi) is `reserved_12_[0..1]` and `[7..8]` of every record.

`tests/media.cpp` pins this table, so a blob cannot drift from the specification without a
test failure.

## Reference

- **Document:** [Raster Command Reference](https://download.brother.com/welcome/docp000750/cv_td2000_eng_raster_101.pdf)
- **Version:** 1.01
- **Page:** 28
- **PTD source:** `bst202ed.txt` (TD-2020, FormatVersion 1.03.00.00)

## Structure

`drivers::td2000::types::MediaInfoFields` in `src/drivers/td2000/include/td2000.h` maps these
bytes. The Raster Command Reference does not describe the contents of the 127 bytes, so the
field names come from the PTD source and the values are checked against the label table of
section 2.3.2 (b).
