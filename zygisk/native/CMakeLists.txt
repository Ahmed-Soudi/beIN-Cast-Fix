cmake_minimum_required(VERSION 3.22)
project(bein_cast_root LANGUAGES C CXX ASM)

if(NOT ANDROID)
    message(FATAL_ERROR "Use the Android NDK CMake toolchain")
endif()
if(NOT ANDROID_ABI MATCHES "^(arm64-v8a|armeabi-v7a)$")
    message(FATAL_ERROR "This prototype supports arm64-v8a and armeabi-v7a")
endif()
if(NOT ANDROID_STL STREQUAL "c++_static")
    message(FATAL_ERROR "Build with -DANDROID_STL=c++_static; no shared C++ dependency")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor" CACHE PATH "Pinned dependency directory")
set(LSPLANT_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(DEX_BUILDER_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(Plugin.SymbolResolver OFF CACHE BOOL "" FORCE)
set(Plugin.ImportTableReplace OFF CACHE BOOL "" FORCE)
set(Plugin.Android.BionicLinkerUtil OFF CACHE BOOL "" FORCE)

add_subdirectory("${VENDOR_DIR}/lsplant/lsplant/src/main/jni" lsplant EXCLUDE_FROM_ALL)
add_subdirectory("${VENDOR_DIR}/dobby" dobby EXCLUDE_FROM_ALL)

set(XZ_DIR "${VENDOR_DIR}/xz-embedded")
add_library(bein_xz STATIC
    "${XZ_DIR}/linux/lib/xz/xz_crc32.c"
    "${XZ_DIR}/linux/lib/xz/xz_crc64.c"
    "${XZ_DIR}/linux/lib/xz/xz_sha256.c"
    "${XZ_DIR}/linux/lib/xz/xz_dec_stream.c"
    "${XZ_DIR}/linux/lib/xz/xz_dec_lzma2.c"
    "${XZ_DIR}/linux/lib/xz/xz_dec_bcj.c")
target_include_directories(bein_xz PUBLIC "${XZ_DIR}/linux/include/linux"
    PRIVATE "${XZ_DIR}/userspace")
target_compile_definitions(bein_xz PUBLIC XZ_USE_CRC64 XZ_USE_SHA256 PRIVATE XZ_DEC_SINGLE
    XZ_DEC_X86 XZ_DEC_ARM XZ_DEC_ARMTHUMB XZ_DEC_ARM64)
set_property(TARGET bein_xz PROPERTY C_STANDARD 11)

add_library(bein_cast_root SHARED main.cpp)
# Dobby sets C++11 in its own directory; our source and LSPlant require C++20.
set_property(TARGET bein_cast_root PROPERTY CXX_STANDARD 20)
target_include_directories(bein_cast_root PRIVATE "${VENDOR_DIR}/zygisk/module/jni")
target_compile_options(bein_cast_root PRIVATE
    -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-omit-frame-pointer)
target_link_options(bein_cast_root PRIVATE
    -Wl,--gc-sections -Wl,--no-undefined -Wl,--exclude-libs,ALL
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
target_link_libraries(bein_cast_root PRIVATE lsplant_static dobby bein_xz log dl z)
