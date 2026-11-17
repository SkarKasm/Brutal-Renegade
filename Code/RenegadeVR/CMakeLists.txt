# RenegadeVR.dll - OpenXR bridge for Renegade.exe.
#
# Must be built as 32-bit to match Renegade.exe:
#   cmake -S Code/RenegadeVR -B Code/RenegadeVR/build -A Win32
#   cmake --build Code/RenegadeVR/build --config Release
#
# The OpenXR loader is built from the Khronos OpenXR-SDK (Apache-2.0).
# Pass -DRVR_OPENXR_SDK_DIR=<path to an OpenXR-SDK source tree> to use a local
# copy instead of downloading it.

cmake_minimum_required(VERSION 3.20)
project(RenegadeVR LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "RenegadeVR must be built 32-bit (use -A Win32) to load into Renegade.exe.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT, like the rest of Renegade (/MT), so the DLL has no runtime redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(RVR_OPENXR_SDK_DIR "" CACHE PATH "Local OpenXR-SDK source tree (optional)")
set(RVR_OPENXR_SDK_TAG "release-1.1.63" CACHE STRING "OpenXR-SDK tag to download when no local tree is given")

# Visual Studio's ZERO_CHECK (re-run CMake when CMakeLists.txt changes) is a cmd script that
# breaks on paths with '&', like "Command & Conquer". Re-run the configure step by hand instead.
set(CMAKE_SUPPRESS_REGENERATION ON)

# RenegadeVR.dll and the OpenXR loader DLL land in the same folder, ready to copy into the game.
# (A post-build copy command would break on '&' in paths the same way.)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)

if(RVR_OPENXR_SDK_DIR)
	add_subdirectory(${RVR_OPENXR_SDK_DIR} openxr-sdk EXCLUDE_FROM_ALL)
else()
	include(FetchContent)
	FetchContent_Declare(openxr_sdk
		GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
		GIT_TAG ${RVR_OPENXR_SDK_TAG}
		GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(openxr_sdk)
endif()

add_library(RenegadeVR SHARED
	renegade_vr.cpp
	renegade_vr_api.h)

target_compile_definitions(RenegadeVR PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MINGW)
	# MinGW / llvm-mingw: no separate C++ runtime DLLs to ship, and plain C export names
	target_link_options(RenegadeVR PRIVATE -static -Wl,--kill-at)
	set_target_properties(RenegadeVR PROPERTIES PREFIX "")
	target_link_options(openxr_loader PRIVATE -static)
endif()
target_link_libraries(RenegadeVR PRIVATE openxr_loader d3d11 dxgi)
# Loads RenegadeVR.dll like the game does and submits test images; see rvr_smoketest.c
enable_language(C)
add_executable(rvr_smoketest rvr_smoketest.c renegade_vr_api.h)
if(MINGW)
	target_link_options(rvr_smoketest PRIVATE -static)
endif()

if(TARGET OpenXR::headers)
	target_link_libraries(RenegadeVR PRIVATE OpenXR::headers)
endif()
