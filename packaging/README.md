# Packaging

## Arch
```bash
sudo pacman -S cmake gcc openssl alsa-lib libpulse gtk3 webkit2gtk-4.1 curl
cd packaging && makepkg -si -p PKGBUILD.local
```

## Debian / Ubuntu
```bash
sudo apt install cmake g++ pkg-config libssl-dev libasound2-dev libpulse-dev \
  libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev file dpkg-dev
cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-deb -j"$(nproc)" && cd build-deb && cpack -G DEB
```

## Fedora / RHEL
```bash
sudo dnf install cmake gcc-c++ pkgconf-pkg-config openssl-devel alsa-lib-devel \
  libpulse-devel gtk3-devel webkit2gtk4.1-devel libcurl-devel rpm-build
cmake -S . -B build-rpm -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-rpm -j"$(nproc)" && cd build-rpm && cpack -G RPM
```

