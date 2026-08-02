# PDL — Pager Data Linux

Native Linux pager decoder. **POCSAG** is the supported decoder; FLEX, ACARS, MOBITEX, and ERMES are present in the codebase but disabled until verified. GTK3 GUI (classic) or WebKit UI, ALSA/PulseAudio capture, optional PagerCast stream integration.

## Version

`PDL <major.minor.patch> (Linux · POCSAG)` — set in CMake (`PDL_VERSION`).

## Requirements

- CMake ≥ 3.10
- C++ compiler
- OpenSSL
- ALSA (libasound2-dev)
- PulseAudio (libpulse-dev)
- GTK3 (gtk+-3.0)
- WebKitGTK (webkit2gtk-4.1 or 4.0)
- libcurl

### Install dependencies

**Fedora / RHEL**
```bash
sudo dnf install cmake gcc-c++ openssl-devel alsa-lib-devel libpulse-devel gtk3-devel webkit2gtk4.1-devel libcurl-devel
```

**Debian / Ubuntu**
```bash
sudo apt install cmake g++ libssl-dev libasound2-dev libpulse-dev libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev
```

**Arch**
```bash
sudo pacman -S cmake gcc openssl alsa-lib libpulse gtk3 webkit2gtk-4.1 curl
```

## Build

```bash
mkdir -p build && cd build
cmake ..
cmake --build . -j$(nproc)
```

Binary: `./build/pdl`

## Settings

Settings are stored in `pdl.ini`.

```bash
export PDL_POCSAG_DECRYPT_KEY="your-secret-key"
```

## Packages

See [`packaging/README.md`](packaging/README.md).

## License

Same as the upstream Discriminator project where applicable.

