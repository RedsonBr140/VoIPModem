# VoIPModem: Linux build (docs/LINUX.md).
#
# Windows builds use VoIPModem.sln; this file is for Linux only.
#
#   cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-linux -j
#   ctest --test-dir build-linux --output-on-failure
#
# Dependencies come from the submodules, as on Windows:
#   third_party/pjproject    PJSIP, through its own CMake build, with
#                            VoIPModem's config_site.h
#   third_party/libv90modem  the softmodem, through its own Makefile
# Run `git submodule update --init third_party/pjproject
# third_party/libv90modem` after cloning.

# PJSIP's CMake build needs 3.28
cmake_minimum_required(VERSION 3.28)
project(VoIPModem LANGUAGES C CXX)

if(WIN32)
  message(FATAL_ERROR "On Windows, build VoIPModem.sln (see README.md).")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VM_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
foreach(dep pjproject libv90modem)
  if(NOT EXISTS ${VM_THIRD_PARTY}/${dep}/.git)
    message(FATAL_ERROR "third_party/${dep} is not checked out. Run: "
      "git submodule update --init third_party/pjproject third_party/libv90modem")
  endif()
endforeach()

# --- PJSIP -----------------------------------------------------------------
#
# pjproject's build reads pjlib/include/pj/config_site.h from its own source
# tree and does not track it in git; copy VoIPModem's in, as
# scripts/setup-pjproject.ps1 does on Windows.
configure_file(${VM_THIRD_PARTY}/pjsip-config/config_site.h
               ${VM_THIRD_PARTY}/pjproject/pjlib/include/pj/config_site.h COPYONLY)

# Only what VoIPModem uses: SIP, RTP, G.711 and the null sound device (calls
# never touch a sound card). No video, no codecs or features that pull in
# system libraries. set(... CACHE) leaves a value given with -D alone.
set(PJ_BUILD_APPS OFF CACHE BOOL "")
set(PJ_BUILD_PJSUA2 ON CACHE BOOL "")
set(PJ_BUILD_SWIG_JAVA OFF CACHE BOOL "")
set(PJ_BUILD_SWIG_PYTHON OFF CACHE BOOL "")
set(PJ_BUILD_SWIG_CSHARP OFF CACHE BOOL "")
set(PJLIB_WITH_SSL none CACHE STRING "")
set(PJNATH_WITH_UPNP OFF CACHE BOOL "")
set(PJMEDIA_WITH_VIDEO OFF CACHE BOOL "")
set(PJMEDIA_WITH_AUDIODEV ON CACHE BOOL "")
set(PJMEDIA_WITH_AUDIODEV_NULL ON CACHE BOOL "")
set(PJMEDIA_WITH_AUDIODEV_ALSA OFF CACHE BOOL "")
set(PJMEDIA_WITH_AUDIODEV_PORTAUDIO OFF CACHE BOOL "")
foreach(codec OPUS SILK BCG729 OPENCORE_AMRNB OPENCORE_AMRWB LYRA VPX OPEN_H264)
  set(PJMEDIA_WITH_${codec}_CODEC OFF CACHE BOOL "")
endforeach()
set(PJMEDIA_WITH_FFMPEG OFF CACHE BOOL "")
set(PJMEDIA_WITH_LIBYUV OFF CACHE BOOL "")
set(PJMEDIA_WITH_WEBRTC_AEC OFF CACHE BOOL "")
set(PJMEDIA_WITH_WEBRTC_AEC3 OFF CACHE BOOL "")
add_subdirectory(${VM_THIRD_PARTY}/pjproject ${CMAKE_BINARY_DIR}/pjproject EXCLUDE_FROM_ALL SYSTEM)

# --- libv90modem -------------------------------------------------------------
#
# Its own Makefile, building only the static library, into our build tree.
include(ExternalProject)
set(V90M_BUILD ${CMAKE_BINARY_DIR}/libv90modem)
set(V90M_LIB ${V90M_BUILD}/libv90modem.a)
ExternalProject_Add(libv90modem_build
  SOURCE_DIR ${VM_THIRD_PARTY}/libv90modem
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make -C <SOURCE_DIR> BUILD=${V90M_BUILD} CC=${CMAKE_C_COMPILER} ${V90M_LIB}
  BUILD_ALWAYS ON
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS ${V90M_LIB}
)
add_library(libv90modem STATIC IMPORTED GLOBAL)
set_target_properties(libv90modem PROPERTIES
  IMPORTED_LOCATION ${V90M_LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${VM_THIRD_PARTY}/libv90modem/include
  INTERFACE_LINK_LIBRARIES m)
add_dependencies(libv90modem libv90modem_build)

# --- VoIPModem library ---------------------------------------------------------
#
# src/Lib without the Windows-only parts (the COM port and the modem speaker).
add_library(voipmodem_lib STATIC
  src/Lib/Config/AppConfig.cpp
  src/Lib/Config/IniConfig.cpp
  src/Lib/Config/IniDocument.cpp
  src/Lib/Control/ControlServer.cpp
  src/Lib/Control/ServiceStats.cpp
  src/Lib/Control/SystemInfo.cpp
  src/Lib/Core/ModemStateMachine.cpp
  src/Lib/Core/ResultCodes.cpp
  src/Lib/Dsp/Dds.cpp
  src/Lib/Dsp/DtmfGenerator.cpp
  src/Lib/Dsp/GoertzelDetector.cpp
  src/Lib/Dsp/ToneGenerator.cpp
  src/Lib/Hayes/AtCommandParser.cpp
  src/Lib/Logging/Logger.cpp
  src/Lib/Modem/LoopbackTest.cpp
  src/Lib/Sip/CallAudioBridge.cpp
  src/Lib/Sip/PjsipCallController.cpp
  src/Lib/Sip/PjsipEndpoint.cpp
)
# third_party: <nlohmann/json.hpp> (vendored)
target_include_directories(voipmodem_lib PUBLIC ${VM_THIRD_PARTY})
target_link_libraries(voipmodem_lib PUBLIC Pj::pjsua2 libv90modem)
# Designated-style partial initialisers of AtCommand are deliberate.
target_compile_options(voipmodem_lib PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# --- Unit tests ----------------------------------------------------------------
add_executable(voipmodem_tests
  tests/main.cpp
  tests/AppConfigTests.cpp
  tests/AtCommandParserTests.cpp
  tests/ControlSignalTests.cpp
  tests/ControlTests.cpp
  tests/DspPrimitiveTests.cpp
  tests/DtmfGeneratorTests.cpp
  tests/IniDocumentTests.cpp
  tests/LibV90ModemTests.cpp
  tests/ModemStateMachineTests.cpp
)
target_link_libraries(voipmodem_tests PRIVATE voipmodem_lib)

enable_testing()
add_test(NAME unit COMMAND voipmodem_tests)
