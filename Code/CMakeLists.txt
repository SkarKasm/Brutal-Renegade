# Renegade.exe with a modern MSVC (Visual Studio 2022/2026), 32-bit.
#
# The original build is VC++ 6 (commando.dsw). This builds the same Release sources
# (Port/sources.cmake, generated from the .dsp files by gen_sources.py) with compiler switches
# that keep VC6 behaviour, plus Port/compat for SDKs that aren't in the tree.
# See Code/Port/README.md.
#
# From an x86 Native Tools prompt (Ninja: the Visual Studio generator breaks on '&' in paths):
#   cmake -S Code -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DRENEGADE_GAME_DIR=<Renegade install>
#   cmake --build <build>

cmake_minimum_required(VERSION 3.20)
# Compile the migration contracts on either architecture without linking the
# legacy x86 game or enabling its RC/audio/video/assembly dependencies.
option(BR_ARCHITECTURE_ONLY "Build only the x86/x64 architecture foundation headers" OFF)
if(BR_ARCHITECTURE_ONLY)
    project(RenegadeArchitecture LANGUAGES CXX)
    add_subdirectory(Port/architecture)
    return()
endif()
project(Renegade LANGUAGES C CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "Renegade is 32-bit only: use the x86 toolchain.")
endif()
if(NOT MSVC)
	message(FATAL_ERROR "This port targets MSVC.")
endif()

set(CMAKE_SUPPRESS_REGENERATION ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")		# /MT like the original
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(RENEGADE_CODE "${CMAKE_CURRENT_SOURCE_DIR}")
set(PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Port")
set(RENEGADE_GAME_DIR "" CACHE PATH "A Renegade install (for Mss32.dll / binkw32.dll import libraries)")

include(${PORT_DIR}/sources.cmake)

# VC6 compatibility: old for-scope, no two-phase lookup, wchar_t as unsigned short, string
# literals as char*, permissive conversions. Warnings off: this is 2002 code.
set(RENEGADE_CXX_FLAGS
	/permissive /Zc:forScope- /Zc:twoPhase- /Zc:wchar_t- /Zc:strictStrings- /Zc:rvalueCast-
	/Zc:sizedDealloc- /Zc:threadSafeInit- /Zc:hiddenFriend- /EHsc /W0 /MP /bigobj
	/Zi /FS /Oy- /we4731)		# PDBs and frame pointers for crash diagnosis (Release keeps /O2)
set(RENEGADE_DEFINES
	WIN32 _WINDOWS NDEBUG _MBCS _WIN32_WINNT=0x0601 WINVER=0x0601
	_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
	_USE_32BIT_TIME_T DIRECTX
	_CONST_RETURN=)		# strchr/strstr/... return char* as in VC6, not const char*

set(RENEGADE_INCLUDES
	${PORT_DIR}/compat
	${PORT_DIR}/compat/d3d8
	${PORT_DIR}/compat/mss
	${PORT_DIR}/compat/bink
	${RENEGADE_CODE}
	${RENEGADE_CODE}/wwlib ${RENEGADE_CODE}/WWMath ${RENEGADE_CODE}/wwdebug ${RENEGADE_CODE}/wwsaveload
	${RENEGADE_CODE}/wwbitpack ${RENEGADE_CODE}/wwtranslatedb ${RENEGADE_CODE}/wwutil ${RENEGADE_CODE}/wwnet
	${RENEGADE_CODE}/ww3d2 ${RENEGADE_CODE}/wwphys ${RENEGADE_CODE}/WWAudio ${RENEGADE_CODE}/wwui
	${RENEGADE_CODE}/Combat ${RENEGADE_CODE}/BinkMovie ${RENEGADE_CODE}/SControl ${RENEGADE_CODE}/Commando
	${RENEGADE_CODE}/wolapi ${RENEGADE_CODE}/WWOnline ${RENEGADE_CODE}/WOLBrowser ${RENEGADE_CODE}/BandTest)

function(renegade_target target)
	target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${RENEGADE_CXX_FLAGS}> $<$<COMPILE_LANGUAGE:C>:/W0>)
	target_compile_definitions(${target} PRIVATE ${RENEGADE_DEFINES})
	target_include_directories(${target} PRIVATE ${RENEGADE_INCLUDES})
endfunction()

foreach(lib wwdebug wwlib wwmath wwsaveload wwbitpack wwtranslatedb wwutil wwnet ww3d2 wwphys wwaudio wwui combat binkmovie scontrol)
	string(TOUPPER ${lib} LIBU)
	add_library(${lib} STATIC ${${LIBU}_SOURCES})
	renegade_target(${lib})
endforeach()

# Shared CPU implementation and the native per-instance DX8 bridge. Kept out of
# legacy compiler modes; only these new translation units require C++17.
add_library(renegade_voxel STATIC VoxelGore/renegade_voxel.cpp
    VoxelGore/voxel_draw.cpp VoxelGore/gore/gorecore.cpp)
target_compile_features(renegade_voxel PRIVATE cxx_std_17)
target_compile_options(renegade_voxel PRIVATE /EHsc /W3)
target_sources(ww3d2 PRIVATE ww3d2/voxel_skin.cpp)
set_source_files_properties(ww3d2/voxel_skin.cpp PROPERTIES COMPILE_OPTIONS "/std:c++17;/Zc:forScope")
target_link_libraries(ww3d2 PUBLIC renegade_voxel)

# SDKs that aren't in the tree (see compat/README.md)
add_library(renegade_compat STATIC
	Port/compat/d3dx8_compat.cpp
	Port/compat/gamespy_stub.cpp
	Port/compat/regex_compat.cpp
	Port/compat/mss_autocleanup.cpp)
renegade_target(renegade_compat)

# scripts.dll: the mission scripts. It has its own copies of scripts.h / scriptcommands.h,
# so it doesn't get the engine's include path.
add_library(scripts SHARED ${SCRIPTS_SOURCES})
target_compile_options(scripts PRIVATE ${RENEGADE_CXX_FLAGS})
target_compile_definitions(scripts PRIVATE ${RENEGADE_DEFINES} BUILDING_DLL)
target_include_directories(scripts PRIVATE ${RENEGADE_CODE}/Scripts ${PORT_DIR}/compat)

# bandtest.dll (network bandwidth test), built from its own sources like the original
add_library(BandTest SHARED BandTest/BandTest.cpp BandTest/BandTest.rc)
renegade_target(BandTest)
target_compile_definitions(BandTest PRIVATE _USRDLL BANDTEST_EXPORTS)
target_link_libraries(BandTest PRIVATE ws2_32 winmm)

add_executable(Renegade WIN32 ${COMMANDO_SOURCES})
renegade_target(Renegade)
target_link_libraries(Renegade PRIVATE
	combat wwui wwaudio ww3d2 wwphys wwsaveload wwnet wwbitpack wwtranslatedb wwutil binkmovie scontrol
	wwmath wwlib wwdebug renegade_compat BandTest
	dinput8 dxguid dsound winmm vfw32 wsock32 ws2_32 version user32 gdi32 winspool comdlg32 advapi32
	ole32 oleaut32 uuid shell32 imm32)
target_link_options(Renegade PRIVATE /SAFESEH:NO /LARGEADDRESSAWARE /DEBUG /OPT:REF /OPT:ICF /DYNAMICBASE:NO)	# fixed base like the VC6 original
target_link_options(scripts PRIVATE /DEBUG /OPT:REF /OPT:ICF)

# Offline CPU-only skinning parity tool. Never creates a renderer/window and
# never deploys to a game installation. Explicit opt-in, excluded from all.
option(BR_BUILD_VOXEL_GORE_TOOLS "Enable offline voxel gore engine parity tool" OFF)
if(BR_BUILD_VOXEL_GORE_TOOLS)
    add_executable(voxel_adapter_check EXCLUDE_FROM_ALL Tools/VoxelGore/adapter_check.cpp)
    target_compile_features(voxel_adapter_check PRIVATE cxx_std_17)
    target_link_libraries(voxel_adapter_check PRIVATE renegade_voxel)
    add_executable(voxel_engine_parity EXCLUDE_FROM_ALL Tools/VoxelGore/engine_parity.cpp
        Tools/VoxelGore/headless_audio_stubs.cpp)
    renegade_target(voxel_engine_parity)
    target_compile_features(voxel_engine_parity PRIVATE cxx_std_17)
    target_link_libraries(voxel_engine_parity PRIVATE ww3d2 wwmath wwsaveload wwlib wwdebug
        renegade_compat winmm vfw32)
    target_link_options(voxel_engine_parity PRIVATE /SAFESEH:NO /OPT:REF /OPT:ICF)
    add_executable(voxel_native_bridge_check EXCLUDE_FROM_ALL Tools/VoxelGore/native_bridge_check.cpp
        Tools/VoxelGore/headless_audio_stubs.cpp)
    renegade_target(voxel_native_bridge_check)
    target_compile_features(voxel_native_bridge_check PRIVATE cxx_std_17)
    target_link_libraries(voxel_native_bridge_check PRIVATE ww3d2 wwmath wwsaveload wwlib wwdebug renegade_compat winmm vfw32)
    target_link_options(voxel_native_bridge_check PRIVATE /SAFESEH:NO /OPT:REF /OPT:ICF)
endif()

# Import libraries for the game's own Miles / Bink / BandTest DLLs (made from their exports)
include(${PORT_DIR}/importlibs.cmake)

add_executable(BrutalRenegadeVRLauncher WIN32 Port/renegade_launcher.cpp)
target_compile_definitions(BrutalRenegadeVRLauncher PRIVATE RVR_LAUNCH_VR=1)
set_target_properties(BrutalRenegadeVRLauncher PROPERTIES OUTPUT_NAME "Play Brutal Renegade VR")
target_link_libraries(BrutalRenegadeVRLauncher PRIVATE user32 gdi32 advapi32)
add_executable(BrutalRenegadeLauncher WIN32 Port/renegade_launcher.cpp)
set_target_properties(BrutalRenegadeLauncher PROPERTIES OUTPUT_NAME "Play Brutal Renegade")
target_link_libraries(BrutalRenegadeLauncher PRIVATE user32 gdi32 advapi32)

add_executable(miles_volume_check EXCLUDE_FROM_ALL Port/miles_volume_check.cpp Port/compat/mss_autocleanup.cpp)
target_include_directories(miles_volume_check PRIVATE ${PORT_DIR}/compat/mss)
target_link_libraries(miles_volume_check PRIVATE ${RENEGADE_IMPORTLIB_DIR}/mss32.lib winmm)
