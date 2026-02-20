# PDW-Linux

Native Linux port of **PDW** (Pager Data Decoder), supporting POCSAG, FLEX, ACARS, MOBITEX, and ERMES. GTK3 GUI, ALSA/PulseAudio capture.

## Requirements

- CMake ≥ 2.8  
- C++ compiler  
- OpenSSL  
- ALSA (libasound2-dev)  
- PulseAudio (libpulse-dev)  
- GTK3 (gtk+-3.0)

### Install dependencies

**Fedora / RHEL**
```bash
sudo dnf install cmake gcc-c++ openssl-devel alsa-lib-devel libpulse-devel gtk3-devel
```

**Debian / Ubuntu**
```bash
sudo apt install cmake g++ libssl-dev libasound2-dev libpulse-dev libgtk-3-dev
```

**Arch**
```bash
sudo pacman -S cmake gcc openssl alsa-lib libpulse gtk3
```

## Build

```bash
mkdir build && cd build
cmake ..
make
./pdw_linux
```

## Usage

- **`pdw_linux`** — start with default audio device.  
- **`pdw_linux -v`** or **`pdw_linux --verbose`** — verbose logging (decoded lines, mode, UI actions).  
- **`pdw_linux -v 2`** or **`pdw_linux --verbose full`** — include display pipeline debug.  
- **File → Exit** — quit. **Interface → Setup/Volume** — select audio input. **Options** — decoding options (POCSAG/FLEX/MOBITEX/ACARS). **Display → Clear Screen** — clear panes.

### POCSAG encrypted messages 

Messages encrypted with AES-256-CTR, Base64 are decrypted automatically if you set the key:

```bash
export PDW_POCSAG_DECRYPT_KEY="your-secret-key"
./pdw_linux
```

Same key and format as used when encoding with `pocsag --encrypt --key "your-secret-key"`. If decryption fails (wrong key or unencrypted message), the raw message is shown.

## License

Same as the original PDW project.
