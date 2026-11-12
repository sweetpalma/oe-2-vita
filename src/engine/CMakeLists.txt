cmake_minimum_required(VERSION 3.25)
project(oe2 LANGUAGES C)

# The renderer is software and the game advances per tick; keep it optimized by default.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(OE2_SANITIZE "Build with ASan/UBSan" OFF)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
	pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
	pkg_check_modules(ZLIB QUIET IMPORTED_TARGET zlib)
	# QCELP-13k (all O&E II sounds and music) is decoded with the system libavcodec (LGPL).
	pkg_check_modules(LIBAV QUIET IMPORTED_TARGET libavcodec libavutil)
endif()

if(NOT TARGET PkgConfig::ZLIB)
	find_package(ZLIB REQUIRED)
	add_library(PkgConfig::ZLIB INTERFACE IMPORTED)
	target_link_libraries(PkgConfig::ZLIB INTERFACE ZLIB::ZLIB)
endif()

if(NOT TARGET PkgConfig::SDL2)
	find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2 PATHS /opt/homebrew /usr/local)
	find_library(SDL2_LIBRARY NAMES SDL2 PATHS /opt/homebrew /usr/local)
	if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
		message(FATAL_ERROR "SDL2 not found")
	endif()
	add_library(PkgConfig::SDL2 INTERFACE IMPORTED)
	target_include_directories(PkgConfig::SDL2 INTERFACE "${SDL2_INCLUDE_DIR}")
	target_link_libraries(PkgConfig::SDL2 INTERFACE "${SDL2_LIBRARY}")
endif()

if(NOT TARGET PkgConfig::LIBAV)
	find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h PATHS /opt/homebrew /usr/local)
	find_library(AVCODEC_LIBRARY NAMES avcodec PATHS /opt/homebrew /usr/local)
	find_library(AVUTIL_LIBRARY NAMES avutil PATHS /opt/homebrew /usr/local)
	if(NOT AVCODEC_INCLUDE_DIR OR NOT AVCODEC_LIBRARY OR NOT AVUTIL_LIBRARY)
		message(FATAL_ERROR "libavcodec/libavutil not found")
	endif()
	add_library(PkgConfig::LIBAV INTERFACE IMPORTED)
	target_include_directories(PkgConfig::LIBAV INTERFACE "${AVCODEC_INCLUDE_DIR}")
	target_link_libraries(PkgConfig::LIBAV INTERFACE "${AVCODEC_LIBRARY}" "${AVUTIL_LIBRARY}")
endif()

add_subdirectory(src)
