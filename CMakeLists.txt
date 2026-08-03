# CMakeLists.txt — PDL (Pager Data Linux)
# Native Linux pager decoder: POCSAG (active); FLEX / ACARS / MOBITEX / ERMES stubs

cmake_minimum_required(VERSION 3.10)
project(PDL CXX)

set(PDL_VERSION_MAJOR 3)
set(PDL_VERSION_MINOR 2)
set(PDL_VERSION_PATCH 0)
set(PDL_VERSION "${PDL_VERSION_MAJOR}.${PDL_VERSION_MINOR}.${PDL_VERSION_PATCH}")
string(TIMESTAMP PDL_BUILD_DATE "%Y-%m-%d")
string(TIMESTAMP PDL_BUILD_TIME "%H:%M:%S")

if(NOT UNIX OR APPLE)
  message(FATAL_ERROR "PDL is a Linux port only. This project does not build on Windows or macOS.")
endif()

add_subdirectory(GFX)
add_subdirectory(Headers)
add_subdirectory(utils)

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(PULSE libpulse)
pkg_check_modules(PULSE_SIMPLE libpulse-simple)
if(NOT PULSE_FOUND)
  message(FATAL_ERROR "PulseAudio (libpulse) is required. Install libpulse-dev and re-run cmake.")
endif()
pkg_check_modules(GTK3 gtk+-3.0)
if(NOT GTK3_FOUND)
  message(FATAL_ERROR "GTK3 (gtk+-3.0) is required for the GUI.\n"
    "  Fedora/RHEL:   sudo dnf install gtk3-devel\n"
    "  Debian/Ubuntu: sudo apt install libgtk-3-dev\n"
    "  Arch:          sudo pacman -S gtk3")
endif()
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(WEBKIT webkit2gtk-4.1)
if(NOT WEBKIT_FOUND)
  pkg_check_modules(WEBKIT webkit2gtk-4.0)
endif()
if(NOT WEBKIT_FOUND)
  message(FATAL_ERROR "WebKitGTK (webkit2gtk-4.1 or 4.0) is required for the modern Web UI.\n"
    "  Fedora/RHEL:   sudo dnf install webkit2gtk4.1-devel\n"
    "  Debian/Ubuntu: sudo apt install libwebkit2gtk-4.1-dev\n"
    "  Arch:          sudo pacman -S webkit2gtk-4.1")
endif()

# Spectrum waterfall uses a cheap CPU FFT only (Vulkan path froze the GTK UI).

set(PDL_LINUX_SRCS
  Flex.cpp
  Misc.cpp
  Pocsag.cpp
  acars.cpp
  decode.cpp
  ermes.cpp
  helper_funcs.cpp
  language.cpp
  mobitex.cpp
  sound_in.cpp
  utils/Debug.cpp
  utils/Ostype.cpp
  utils/binary.cpp
  linux/rs232_linux.cpp
  linux/hw_decode.cpp
  linux/gpu_spectrum.cpp
  linux/smtp_linux.cpp
  linux/init_linux.cpp
  linux/stubs_linux.cpp
  linux/settings_linux.cpp
  linux/pdl_platform_linux.cpp
  linux/pocsag_decrypt.cpp
  linux/audio_alsa.cpp
  linux/audio_pulse.cpp
  linux/audio_playback.cpp
  linux/gui_gtk.cpp
  linux/gui_web.cpp
  linux/feature_dialogs.cpp
  linux/pagercast_stream.cpp
  linux/main_linux.cpp
)

include(GNUInstallDirs)

add_executable(pdl ${PDL_LINUX_SRCS})
configure_file(
  ${CMAKE_SOURCE_DIR}/linux/pdl_version.h.in
  ${CMAKE_BINARY_DIR}/generated/pdl_version.h
  @ONLY
)
target_compile_definitions(pdl PRIVATE
  __linux__
  PDL_LINUX_HAVE_GTK
  "PDL_DATADIR=\"${CMAKE_INSTALL_FULL_DATADIR}/pdl\""
)
target_include_directories(pdl PRIVATE
  ${CMAKE_BINARY_DIR}/generated
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/Headers
  ${CMAKE_SOURCE_DIR}/utils
  ${CMAKE_SOURCE_DIR}/platform
  ${ALSA_INCLUDE_DIRS}
  ${PULSE_INCLUDE_DIRS}
  ${GTK3_INCLUDE_DIRS}
  ${WEBKIT_INCLUDE_DIRS}
  ${CURL_INCLUDE_DIRS}
)
target_link_libraries(pdl
  OpenSSL::SSL
  OpenSSL::Crypto
  ${ALSA_LIBRARIES}
  ${PULSE_LIBRARIES}
  ${PULSE_SIMPLE_LIBRARIES}
  pthread
  ${GTK3_LIBRARIES}
  ${WEBKIT_LIBRARIES}
  ${CURL_LIBRARIES}
  m
)

# Ship webui + app icon next to the binary (dev builds)
add_custom_command(TARGET pdl POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/webui
    $<TARGET_FILE_DIR:pdl>/webui
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/GFX/pdl.ico
    $<TARGET_FILE_DIR:pdl>/pdl.ico
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/GFX/pdl.png
    $<TARGET_FILE_DIR:pdl>/pdl.png
  COMMENT "Copying webui + icon next to pdl"
)

# Packaged layout: /usr/bin/pdl + /usr/share/pdl/webui
install(TARGETS pdl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/webui/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pdl/webui
  FILES_MATCHING
    PATTERN "*.html"
    PATTERN "*.js"
    PATTERN "*.css"
    PATTERN "*.svg"
    PATTERN "*.png"
    PATTERN "*.ico"
)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/pdl.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# App icon: source of truth is GFX/pdl.ico; PNGs for freedesktop/hicolor
install(FILES ${CMAKE_SOURCE_DIR}/GFX/pdl.ico
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
install(FILES ${CMAKE_SOURCE_DIR}/GFX/pdl.png
  DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)
foreach(_isz 16 24 32 48 64 128 256)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/icons/hicolor/${_isz}x${_isz}/apps/pdl.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_isz}x${_isz}/apps)
endforeach()

# ---- CPack: .deb + .rpm (build on the target distro/arch) ----
set(CPACK_PACKAGE_NAME "pdl")
set(CPACK_PACKAGE_VENDOR "PDL")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Pager Data Linux — POCSAG pager decoder")
set(CPACK_PACKAGE_VERSION "${PDL_VERSION}")
set(CPACK_PACKAGE_CONTACT "pdl@localhost")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
  "libgtk-3-0, libasound2, libpulse0, libcurl4, libssl3, libwebkit2gtk-4.1-0 | libwebkit2gtk-4.0-37")

set(CPACK_RPM_PACKAGE_LICENSE "Custom")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Communications")
set(CPACK_RPM_PACKAGE_REQUIRES
  "gtk3, alsa-lib, pulseaudio-libs, libcurl, openssl-libs, webkit2gtk4.1")

include(CPack)
