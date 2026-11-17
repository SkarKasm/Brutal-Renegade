cmake_minimum_required(VERSION 3.20)
project(BrutalRenegadeArchitecture LANGUAGES CXX)

# Independent of Code/CMakeLists.txt's x86 dependencies, RC, _USE_32BIT_TIME_T,
# old assembly and DLL import libraries. This target never produces a game exe.
set(BR_EXPECT_POINTER_BITS "" CACHE STRING "Required pointer width: 32 or 64")
if(NOT BR_EXPECT_POINTER_BITS STREQUAL "" AND
   NOT BR_EXPECT_POINTER_BITS STREQUAL "32" AND NOT BR_EXPECT_POINTER_BITS STREQUAL "64")
    message(FATAL_ERROR "BR_EXPECT_POINTER_BITS must be empty, 32 or 64")
endif()
add_library(br_architecture_headers INTERFACE)
target_include_directories(br_architecture_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(br_architecture_headers INTERFACE cxx_std_17)
add_library(br_architecture_contracts OBJECT architecture_contracts.cpp)
target_link_libraries(br_architecture_contracts PRIVATE br_architecture_headers)
if(NOT BR_EXPECT_POINTER_BITS STREQUAL "")
    target_compile_definitions(br_architecture_contracts PRIVATE BR_EXPECT_POINTER_BITS=${BR_EXPECT_POINTER_BITS})
endif()
if(MSVC)
    target_compile_options(br_architecture_contracts PRIVATE /W4 /WX /permissive- /EHsc)
endif()
