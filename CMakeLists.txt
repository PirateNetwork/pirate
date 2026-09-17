# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Migration in progress: this CMake build system is replacing the autotools
# build (configure.ac / Makefile.am). See project memory qt6-cmake-migration.
# Milestone 2a: configure only -- detect the depends toolchain, find the static
# Qt 6, run the platform feature checks and generate src/config/bitcoin-config.h.

cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Use a separate build directory.")
endif()

# Do not let CMake's own tests miss the APPEND_* flags.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  CMAKE_CXX_COMPILE_OBJECT
  CMAKE_CXX_LINK_EXECUTABLE
)

#=============================
# Project / package metadata
#=============================
set(CLIENT_NAME "Pirate")
set(CLIENT_VERSION_MAJOR 6)
set(CLIENT_VERSION_MINOR 0)
set(CLIENT_VERSION_REVISION 4)
set(CLIENT_VERSION_BUILD 50)
set(CLIENT_VERSION_IS_RELEASE "true")
set(COPYRIGHT_YEAR 2026)
set(COPYRIGHT_HOLDERS "The %s developers")
set(COPYRIGHT_HOLDERS_SUBSTITUTION "Pirate")
set(PACKAGE_BUGREPORT "https://github.com/PirateNetwork/Pirate/issues")

project(Pirate
  VERSION ${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_REVISION}
  DESCRIPTION "Pirate Chain (TreasureChest) client software"
  HOMEPAGE_URL "https://pirate.black/"
  LANGUAGES NONE
)

set(PACKAGE_NAME "${CLIENT_NAME}")
set(PACKAGE_VERSION "${PROJECT_VERSION}")
set(PACKAGE_STRING "${CLIENT_NAME} ${PROJECT_VERSION}")
set(PACKAGE_TARNAME "pirate")
set(PACKAGE_URL "")
set(CLIENT_VERSION_STRING "${PROJECT_VERSION}")

#=============================
# Language setup
#=============================
enable_language(C)
enable_language(CXX)
if(WIN32)
  # For the qt/res/*.rc + per-executable *-res.rc resource scripts.
  enable_language(RC)
  if(NOT CMAKE_RC_COMPILER)
    set(CMAKE_RC_COMPILER windres)
  endif()
  set(CMAKE_RC_COMPILE_OBJECT
    "<CMAKE_RC_COMPILER> -O coff <DEFINES> <INCLUDES> <FLAGS> <SOURCE> <OBJECT>"
  )
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/module)
include(ProcessConfigurations)

# Flatten static-lib dependency chains so objects can compile in parallel.
if(NOT DEFINED CMAKE_OPTIMIZE_DEPENDENCIES)
  set(CMAKE_OPTIMIZE_DEPENDENCIES TRUE)
endif()

# Everything we link is static except a handful of base system libraries
# (fontconfig, libc, ...) that are effectively universal on any target Linux
# desktop. Do not bake the depends build tree's own absolute path into the
# binary as an RPATH/RUNPATH -- let the dynamic linker use its normal system
# search path, the way a real release build must.
set(CMAKE_SKIP_RPATH TRUE)

#=============================
# Configurable options
#=============================
include(CMakeDependentOption)
option(BUILD_DAEMON "Build pirated executable." ON)
option(BUILD_CLI "Build pirate-cli executable." ON)
option(BUILD_TX "Build pirate-tx executable." ON)
option(BUILD_GUI "Build pirate-qt executable." OFF)
option(BUILD_GTEST "Build pirate-gtest executable." ON)

option(ENABLE_WALLET "Enable wallet." ON)
option(ENABLE_ZMQ "Enable ZMQ notifications." ON)
option(ENABLE_MINING "Enable mining code." ON)
option(ENABLE_EMBEDDED_TOR "Bundle and auto-manage a Tor daemon." ON)
option(ENABLE_EMBEDDED_I2PD "Bundle and auto-manage an I2P (i2pd) daemon." ON)
cmake_dependent_option(USE_QRCODE "Enable QR code support in the GUI." ON "BUILD_GUI" OFF)
cmake_dependent_option(WITH_DBUS "Enable DBus support in the GUI." ON "BUILD_GUI;NOT APPLE;NOT WIN32" OFF)
option(WITH_SYSTEM_COMMAND "Allow -blocknotify/-alertnotify to run their configured command (util.cpp runCommand())." OFF)

set(APPEND_CPPFLAGS "" CACHE STRING "Preprocessor flags appended after all build-system flags.")
set(APPEND_CXXFLAGS "" CACHE STRING "C++ compiler flags appended after all build-system flags.")
set(APPEND_LDFLAGS  "" CACHE STRING "Linker flags appended after all build-system flags.")
string(APPEND CMAKE_CXX_COMPILE_OBJECT " ${APPEND_CPPFLAGS} ${APPEND_CXXFLAGS}")
string(APPEND CMAKE_CXX_LINK_EXECUTABLE " ${APPEND_LDFLAGS}")

#=============================
# Required C++ toolchain features
#=============================
include(CheckCXXFeatures)
check_cxx_features()

#=============================
# Core interface library
#=============================
add_library(core_interface INTERFACE)
add_library(core_interface_relwithdebinfo INTERFACE)
add_library(core_interface_debug INTERFACE)
target_link_libraries(core_interface INTERFACE
  $<$<CONFIG:RelWithDebInfo>:core_interface_relwithdebinfo>
  $<$<CONFIG:Debug>:core_interface_debug>
)
# The tree gates "#include \"config/bitcoin-config.h\"" on HAVE_CONFIG_H, which
# autotools passed unconditionally.
target_compile_definitions(core_interface INTERFACE HAVE_CONFIG_H ${DEPENDS_COMPILE_DEFINITIONS})
if(WITH_SYSTEM_COMMAND)
  target_compile_definitions(core_interface INTERFACE ENABLE_SYSTEM_COMMAND)
endif()

# The depends prefix. The toolchain file points CMAKE_FIND_ROOT_PATH at it; a few
# headers (rust/cxx.h from the rustcxx package, bdb, ...) ship no CMake/pkg-config
# metadata, so put its include/ and lib/ on the global search path the way
# autotools' config.site did with CPPFLAGS/LDFLAGS.
list(GET CMAKE_FIND_ROOT_PATH 0 DEPENDS_PREFIX)
if(DEPENDS_PREFIX AND IS_DIRECTORY "${DEPENDS_PREFIX}/include")
  target_include_directories(core_interface INTERFACE ${DEPENDS_PREFIX}/include)
  target_link_directories(core_interface INTERFACE ${DEPENDS_PREFIX}/lib)
endif()

# Paths to the tor/i2pd binaries to bundle. depends/packages/{tor,i2pd}.mk build
# these before CMake ever runs, so they are looked up under the depends prefix by
# default; override to bundle a specific binary instead. Left empty when the
# matching embedded daemon is disabled. These feed both the install rules that
# ship them as pirate-tor/pirate-i2pd and the embedded_binary_hashes.h that
# records what this build shipped.
# Windows binaries carry .exe, as depends/config.site.in did with
# embedded_bin_suffix. Without it the paths below name files that do not exist,
# and the embedded_binary_hashes custom command declares them as dependencies
# make has no rule to produce.
if(WIN32)
  set(_embedded_bin_suffix ".exe")
else()
  set(_embedded_bin_suffix "")
endif()

if(ENABLE_EMBEDDED_TOR)
  set(TOR_BINARY_PATH "${DEPENDS_PREFIX}/bin/tor${_embedded_bin_suffix}" CACHE FILEPATH
      "Path to a tor binary to bundle for embedded Tor support.")
else()
  set(TOR_BINARY_PATH "" CACHE FILEPATH "Path to a tor binary to bundle." FORCE)
endif()
if(ENABLE_EMBEDDED_I2PD)
  set(I2PD_BINARY_PATH "${DEPENDS_PREFIX}/bin/i2pd${_embedded_bin_suffix}" CACHE FILEPATH
      "Path to an i2pd binary to bundle for embedded I2P support.")
else()
  set(I2PD_BINARY_PATH "" CACHE FILEPATH "Path to an i2pd binary to bundle." FORCE)
endif()

# pirate-networking only has anything to watch over when at least one embedded
# daemon can be spawned - same condition autotools gated it on.
if(ENABLE_EMBEDDED_TOR OR ENABLE_EMBEDDED_I2PD)
  set(ENABLE_EMBEDDED_NETWORKING_WATCHDOG ON)
else()
  set(ENABLE_EMBEDDED_NETWORKING_WATCHDOG OFF)
endif()

#=============================
# PIC / PIE
#=============================
set(configure_warnings)
include(CheckLinkerSupportsPIE)
check_linker_supports_pie(configure_warnings)

#=============================
# Dependencies
#=============================
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(core_interface INTERFACE Threads::Threads)

# Boost is provided by depends/. Header-only for most of the tree; a few
# components are used directly.
#
# depends builds Boost with runtime-link=static for mingw32 and
# runtime-link=shared elsewhere (depends/packages/boost.mk). BoostConfig.cmake
# rejects the Windows build outright -- "No suitable build variant has been
# found ... static runtime, Boost_USE_STATIC_RUNTIME not ON" -- unless it is
# told which runtime to expect, so match the recipe here.
if(WIN32)
  set(Boost_USE_STATIC_RUNTIME ON)
endif()

find_package(Boost 1.70 REQUIRED COMPONENTS
  system filesystem thread chrono program_options
)
if(Boost_FOUND)
  foreach(v HAVE_BOOST HAVE_BOOST_CHRONO HAVE_BOOST_FILESYSTEM HAVE_BOOST_PROGRAM_OPTIONS
            HAVE_BOOST_SYSTEM HAVE_BOOST_THREAD HAVE_BOOST_UNIT_TEST_FRAMEWORK)
    set(${v} TRUE)
  endforeach()
endif()

if(BUILD_GUI)
  set(qt_components Core Gui Widgets LinguistTools)
  if(ENABLE_WALLET)
    list(APPEND qt_components Network)
  endif()
  if(WITH_DBUS)
    list(APPEND qt_components DBus)
  endif()
  if(BUILD_GTEST)
    list(APPEND qt_components Test)
  endif()
  find_package(Qt 6.2 MODULE REQUIRED COMPONENTS ${qt_components})
  message(STATUS "Found Qt ${Qt6_VERSION} at ${Qt6_DIR}")
  # komodo.cpp's Q_IMPORT_PLUGIN block picks exactly one of these three via
  # #if defined(QT_QPA_PLATFORM_XCB)/_WINDOWS/_COCOA, matching whichever
  # platform plugin src/qt/CMakeLists.txt actually links in
  # (QT_PLATFORM_PLUGIN there is already WIN32/APPLE conditional). Setting
  # QT_QPA_PLATFORM_XCB unconditionally made every platform's static Qt
  # binary reference qt_static_plugin_QXcbIntegrationPlugin(), which is only
  # linked on Linux/BSD -- undefined symbol everywhere else.
  if(WIN32)
    set(QT_QPA_PLATFORM_WINDOWS TRUE)
  elseif(APPLE)
    set(QT_QPA_PLATFORM_COCOA TRUE)
  else()
    set(QT_QPA_PLATFORM_XCB TRUE)
    set(QT_QPA_PLATFORM_MINIMAL TRUE)
  endif()
  set(QT_STATICPLUGIN TRUE)
  unset(qt_components)
endif()

#=============================
# Warning / hardening flags
#=============================
include(TryAppendCXXFlags)
include(TryAppendLinkerFlag)

# pkg-config discovery of depends' static .pc files -- Linux/BSD only.
# autotools deliberately disables pkg-config for MinGW ("does more harm than
# good"), and macOS doesn't get one from depends either; both instead resolve
# depends' libraries directly via find_library() (see FindDepPackage.cmake).
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|FreeBSD|OpenBSD)$")
  find_package(PkgConfig REQUIRED)
  set(ENV{PKG_CONFIG_PATH} "${DEPENDS_PREFIX}/lib/pkgconfig")
  set(ENV{PKG_CONFIG_LIBDIR} "${DEPENDS_PREFIX}/lib/pkgconfig")
endif()

# NOTE: intentionally not DEBUG_LOCKORDER (present upstream in Bitcoin Core,
# where it enables a MultithreadedCChain lock-order-checking wrapper around
# chainActive) -- that wrapper was never ported to this fork (main.h's
# #ifdef DEBUG_LOCKORDER branch references a MultithreadedCChain type that
# doesn't exist anywhere in the tree), and autotools' own --enable-debug never
# defined it either (only plain -DDEBUG). Defining it here broke -DCMAKE_
# BUILD_TYPE=Debug builds with 'MultithreadedCChain does not name a type'.
target_compile_definitions(core_interface_debug INTERFACE DEBUG ABORT_ON_FAILED_ASSUME)

try_append_cxx_flags("-Wall" TARGET core_interface SKIP_LINK)
try_append_cxx_flags("-Wextra" TARGET core_interface SKIP_LINK)
try_append_cxx_flags("-Wformat -Wformat-security" TARGET core_interface SKIP_LINK)
try_append_cxx_flags("-Wvla" TARGET core_interface SKIP_LINK)
try_append_cxx_flags("-Wredundant-decls" TARGET core_interface SKIP_LINK)
try_append_cxx_flags("-Wunused-parameter" TARGET core_interface SKIP_LINK
  IF_CHECK_PASSED "-Wno-unused-parameter"
)

try_append_cxx_flags("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2"
  RESULT_VAR cxx_supports_fortify_source
  SOURCE "int main() {
          #if !defined __OPTIMIZE__ || __OPTIMIZE__ <= 0
            #error
          #endif
          }"
)
if(cxx_supports_fortify_source)
  target_compile_options(core_interface INTERFACE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
endif()
unset(cxx_supports_fortify_source)

try_append_cxx_flags("-fstack-protector-all" TARGET core_interface)
try_append_cxx_flags("-fstack-reuse=none" TARGET core_interface)
try_append_linker_flag("-Wl,-z,relro" TARGET core_interface)
try_append_linker_flag("-Wl,-z,now" TARGET core_interface)
try_append_linker_flag("-Wl,--as-needed" TARGET core_interface)

if(WIN32)
  # PE hardening. configure.ac treated all three as mandatory on mingw
  # (AC_MSG_ERROR if unsupported); without them the shipped Windows binaries
  # lose ASLR and DEP, which no build or test failure would ever reveal.
  try_append_linker_flag("-Wl,--dynamicbase" TARGET core_interface)
  try_append_linker_flag("-Wl,--nxcompat" TARGET core_interface)
  try_append_linker_flag("-Wl,--high-entropy-va" TARGET core_interface)
endif()

#=============================
# Generate the config header
#=============================
include(GenerateConfigHeader)
generate_config_header(${PROJECT_SOURCE_DIR}/src/config/bitcoin-config.h)

target_include_directories(core_interface INTERFACE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/config
)

if(BUILD_GTEST)
  enable_testing()
endif()

add_subdirectory(src)

#=============================
# Configure summary
#=============================
message("\n")
message("Pirate / TreasureChest configure summary")
message("========================================")
message("  Version .............................. ${PACKAGE_STRING} (build ${CLIENT_VERSION_BUILD})")
message("Executables:")
message("  pirated .............................. ${BUILD_DAEMON}")
message("  pirate-cli .......................... ${BUILD_CLI}")
message("  pirate-tx ........................... ${BUILD_TX}")
message("  pirate-qt (GUI) ..................... ${BUILD_GUI}")
message("  pirate-gtest ........................ ${BUILD_GTEST}")
message("Optional features:")
message("  wallet support ...................... ${ENABLE_WALLET}")
message("  ZMQ notifications ................... ${ENABLE_ZMQ}")
message("  mining .............................. ${ENABLE_MINING}")
message("  embedded Tor ....................... ${ENABLE_EMBEDDED_TOR}")
message("  embedded I2P (i2pd) ................ ${ENABLE_EMBEDDED_I2PD}")
message("  QR codes (GUI) ..................... ${USE_QRCODE}")
message("  DBus (GUI) ........................ ${WITH_DBUS}")
message("C++ compiler ......................... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}, ${CMAKE_CXX_COMPILER}")
if(CMAKE_CROSSCOMPILING)
  message("Cross compiling ..................... TRUE, for ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
else()
  message("Cross compiling ..................... FALSE")
endif()
include(FlagsSummary)
flags_summary()
if(configure_warnings)
  message("  ******\n")
  foreach(warning IN LISTS configure_warnings)
    message(WARNING "${warning}")
  endforeach()
  message("  ******\n")
endif()
message("\n")
