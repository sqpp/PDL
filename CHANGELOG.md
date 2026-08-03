# Changelog

## Differences from PDW

Linux port of Windows [PDW](https://www.discriminator.nl/pdw/index-en.html). Baseline: **PDW 3.12**. Own `3.x` version line — not every PDW feature is ported yet.

### Added

- Classic GTK UI and a web UI
- Decode audio from system playback devices (ALSA / Pulse)
- Receive a remote PagerCast stream
- Decode from an audio file
- Encrypted POCSAG messages (AES-256-CTR)
- Arch, Debian/Ubuntu, and Fedora/RHEL packages
- Signal and decode meters, Statistics window, saved counters
- SMTP with TLS / STARTTLS
- Serial port (RS232) support
- CLI: UI choice, file decode, verbose log, polarity flags

### Changes

- Product name **PDL**; settings in `pdl.ini` and `filters.ini`
- Default audio sample rate: **48000** Hz
- Linux only

### Still missing

- Live FLEX, ACARS, MOBITEX, and ERMES decode
- Windows data slicer hardware/driver
- Spectrum / waterfall display
- WAV record, playback, autorecord
- Print, full help, language packs
- Full classic filter automation

---

## TX / RX / protocol

| Role | Component |
|------|-----------|
| TX | PagerBridge |
| RX | PDL (audio → text) |
| Protocol | POCSAG |

## Features

- POCSAG decode
- Decode audio from playback devices (ALSA / Pulse)
- Decode from an audio file
- Classic GTK or web UI
- Receive a PagerCast stream
- Settings in `pdl.ini`
- Encrypted POCSAG messages (AES-256-CTR)

---

## [3.2.0] — 2026-08-03

### Decode

- Call `setupecc()` on Linux so POCSAG BCH correction works
- Fix premature page truncation (idle / mid-page address handling)
- Named POCSAG framing hold-off constants
- `pdl --file` / `-f` offline decode via ffmpeg

### UI

- Statistics dialog; counters persisted in `pdl.ini`
- Rate meter = clean / (clean + corrupt) BCH quality

### Build

- Version **3.2.0**; build date/time at CMake configure
- GitHub Actions: CI + tagged releases (`.deb` / `.rpm`)

## [3.1.0]

- First public Linux alpha (GTK/Web, POCSAG, PagerCast, packaging)
