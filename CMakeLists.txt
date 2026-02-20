# CMakeLists.txt — PDW Linux (native build only)
# Pager Data Decoder: POCSAG, FLEX, ACARS, MOBITEX, ERMES

cmake_minimum_required(VERSION 2.8)
project(PDW CXX)

set(PDW_VERSION_MAJOR 3)
set(PDW_VERSION_MINOR 1)
set(PDW_VERSION_MICRO 0)

if(NOT UNIX OR APPLE)
  message(FATAL_ERROR "PDW-Linux is a Linux port only. This project does not build on Windows or macOS.")
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

set(PDW_LINUX_SRCS
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
  linux/rs232_stub.cpp
  linux/smtp_stub.cpp
  linux/init_linux.cpp
  linux/stubs_linux.cpp
  linux/pdw_platform_linux.cpp
  linux/pocsag_decrypt.cpp
  linux/audio_alsa.cpp
  linux/audio_pulse.cpp
  linux/audio_playback.cpp
  linux/gui_gtk.cpp
  linux/main_linux.cpp
)

add_executable(pdw_linux ${PDW_LINUX_SRCS})
target_compile_definitions(pdw_linux PRIVATE __linux__ PDW_LINUX_HAVE_GTK)
target_include_directories(pdw_linux PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/Headers
  ${CMAKE_SOURCE_DIR}/utils
  ${CMAKE_SOURCE_DIR}/platform
  ${ALSA_INCLUDE_DIRS}
  ${PULSE_INCLUDE_DIRS}
  ${GTK3_INCLUDE_DIRS}
)
target_link_libraries(pdw_linux
  OpenSSL::SSL
  OpenSSL::Crypto
  ${ALSA_LIBRARIES}
  ${PULSE_LIBRARIES}
  ${PULSE_SIMPLE_LIBRARIES}
  pthread
  ${GTK3_LIBRARIES}
)
