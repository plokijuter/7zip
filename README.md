# 7-Zip with a Transpose filter

A fork of [ip7z/7zip](https://github.com/ip7z/7zip) 26.02 adding **method `0C`, Transpose**:
a byte-transposition filter for data made of fixed-size records.

It groups byte *i* of each R-byte record together, so that homogeneous columns
reach the next coder instead of interleaved, unrelated byte streams. Sensor logs,
struct arrays, PCM audio and numeric tables all have that shape, and the existing
Delta filter only covers the special case where R is the stride of a single scalar
series.

- **Work branch:** [`transpose-filter`](https://github.com/plokijuter/7zip/tree/transpose-filter)
- **Diff against upstream:** [`main...transpose-filter`](https://github.com/plokijuter/7zip/compare/main...transpose-filter)
- **Upstream pull request:** [ip7z/7zip#245](https://github.com/ip7z/7zip/pull/245)
- **Binaries and patch:** see [Releases](https://github.com/plokijuter/7zip/releases)

## Results

Measured with `PPMd:o=16` behind the filter:

| data | PPMd alone | with filter | |
|---|---|---|---|
| int32 ascending array (240 KB) | 98 450 | **2 442** | 40x |
| 16-byte struct records (192 KB) | 68 722 | **3 211** | 21x |
| 16-byte struct records (800 KB) | 298 654 | **8 583** | 35x |
| fixed-record `.dat` file (450 KB) | 76 019 | **20 975** | 3.6x |
| 4x float32 sensor log (480 KB) | 399 919 | **215 478** | 1.9x |
| 4x float32 sensor log (48 MB) | 41 876 772 | **11 721 960** | 3.6x |
| random data (200 KB) | 205 808 | 205 824 | identity |
| text | 3 862 | 3 878 | identity |

On the 48 MB sensor log that is also **39% smaller than `xz -9e`** and 66% smaller
than `zstd -19`.

## Usage

```
7zz a -t7z -m0=Transpose -m1=PPMd:o=16 archive.7z file    # R detected automatically
7zz a -t7z -m0=Transpose:15 -m1=PPMd:o=16 archive.7z file # R forced
```

The filter is a preprocessor: pair it with a real coder (`PPMd`, `LZMA`, `LZMA2`)
as `-m1`. Archives are only readable by a build that has this filter.

## How R is chosen

Detection asks whether transposing **helps**, not whether the data is periodic.
It compares the mean absolute difference between bytes R apart against that of
adjacent bytes, and stays at R=1 (identity) unless a column is clearly more
homogeneous, so data with no record structure passes through untouched.

An autocorrelation detector was written first and rejected on measurement: it
found periods everywhere (harmonic sidebands clear a 60%-of-peak threshold) and
degraded 7 of 10 test files, one from 107 607 to 137 309 bytes. The periodicity
of a signal says nothing about the homogeneity of its columns.

## Block size

The transform runs on fixed-size blocks whose size is deliberately independent of
the caller's buffer, since 7-Zip does not use the same buffer sizes when
compressing and decompressing. The size is picked at encoding time from
`kExpectedDataSize` (at most 1/32 of the stream, clamped to 4 KiB..64 KiB) and
recorded in the coder properties, so both sides agree whatever their buffers are.

It is not a single constant because a filter is never told which call is the final
one, so the last partial block of a stream is written through unfiltered. With a
fixed 64 KiB block that tail reached 12.6% of a 450 KB file and cost 7 020 bytes.

Zero-padding the final block through the AES-CBC path in `CFilterCoder::Code` was
considered and rejected: that protocol is only safe because a 16-byte block divides
the FilterCoder buffer, so a full buffer never presents a partial block. A block
that must be a multiple of R has no such property, and an exactly-full buffer would
spin.

## Building

Linux x64:

```
cd CPP/7zip/Bundles/Alone2
make -f makefile.gcc -j$(nproc)
```

Windows x64, cross-compiled from Linux with mingw-w64:

```
cd CPP/7zip/Bundles/Alone2
make -f makefile.gcc IS_MINGW=1 \
     CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ \
     RC=x86_64-w64-mingw32-windres O=_w -j$(nproc)
```

`CROSS_COMPILE=` is not enough on its own here: nothing in the Alone2 makefile
chain includes `var_gcc.mak`, so `CC`/`CXX` fall back to make's defaults and must
be named explicitly. `RC` is hardcoded to `windres.exe` and needs overriding too.

## Verification

60 real files of assorted types, forced R from 2 to 256, sizes
0/1/100/4095/4096/65535/65536/200000, a 48 MB stream, and archives written by an
earlier 1-byte-property build: all extract byte-identical. Archives written on
Linux extract on Windows and back, verified by SHA-256. Compiles clean under
`-Werror -Wall -Wextra`.

## Files

Everything is additive except the build hooks and `DOC/Methods.txt`:

| file | |
|---|---|
| `C/Transpose.c`, `C/Transpose.h` | the transform and the R detector |
| `CPP/7zip/Compress/TransposeFilter.cpp` | the 7-Zip filter, registered at `0C` |
| `CPP/7zip/7zip_gcc.mak`, `CPP/7zip/Bundles/Format7zF/Arc_gcc.mak` | build hooks |
| `DOC/Methods.txt` | method documentation |

## Licence

Same as upstream 7-Zip. The added files are public domain, like the rest of `C/`.
