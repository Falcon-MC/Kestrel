cmake_minimum_required(VERSION 3.24)

project(Kestrel LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR headers-only
)
FetchContent_MakeAvailable(miniaudio)

set(KESTREL_FALCON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Directory containing Falcon-NBT, Falcon-Protocol and Falcon-Network")
if(EXISTS "${KESTREL_FALCON_ROOT}/Falcon-Network/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_FALCON_NBT "${KESTREL_FALCON_ROOT}/Falcon-NBT")
    set(FETCHCONTENT_SOURCE_DIR_FALCON_PROTOCOL "${KESTREL_FALCON_ROOT}/Falcon-Protocol")
    set(FETCHCONTENT_SOURCE_DIR_FALCON_NETWORK "${KESTREL_FALCON_ROOT}/Falcon-Network")
endif()
FetchContent_Declare(
    falcon_network
    GIT_REPOSITORY https://github.com/Falcon-MC/Network.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(falcon_network)

if(EXISTS "${KESTREL_FALCON_ROOT}/Falcon-BedrockData/cmake/FalconBedrockData.cmake")
    set(FETCHCONTENT_SOURCE_DIR_FALCON_BEDROCK_DATA "${KESTREL_FALCON_ROOT}/Falcon-BedrockData")
endif()
FetchContent_Declare(
    falcon_bedrock_data
    GIT_REPOSITORY https://github.com/Falcon-MC/BedrockData.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)
FetchContent_MakeAvailable(falcon_bedrock_data)

set(FALCON_BEDROCK_DATA_PROTOCOL 2193)
include("${falcon_bedrock_data_SOURCE_DIR}/cmake/FalconBedrockData.cmake")
falcon_bedrock_data_embed_binary(FILE block_palette.nbt NAMESPACE KestrelBlockPaletteData
        SYMBOL kBlockPaletteNbt OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockPaletteNbt.h")
falcon_bedrock_data_embed_binary(FILE block_definitions.nbt NAMESPACE KestrelBlockDefinitionData
        SYMBOL kBlockDefinitionsNbt OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockDefinitionsNbt.h")
falcon_bedrock_data_embed_binary(FILE block_light.json NAMESPACE KestrelBlockLightData
        SYMBOL kBlockLightJson OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockLightJson.h")

function(kestrel_embed_file file namespace symbol output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    file(READ "${file}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FALCON_BEDROCK_DATA_BYTES "${hex}")
    string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n"
            FALCON_BEDROCK_DATA_BYTES "${FALCON_BEDROCK_DATA_BYTES}")
    set(FALCON_BEDROCK_DATA_NAMESPACE ${namespace})
    set(FALCON_BEDROCK_DATA_SYMBOL ${symbol})
    configure_file("${FALCON_BEDROCK_DATA_CMAKE_DIR}/EmbeddedBinary.h.in" "${output}" @ONLY)
endfunction()

kestrel_embed_file("${CMAKE_CURRENT_SOURCE_DIR}/data/block_collisions.txt" KestrelBlockCollisionData kBlockCollisions
        "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockCollisionTable.h")
kestrel_embed_file("${CMAKE_CURRENT_SOURCE_DIR}/data/block_breaking.txt" KestrelBlockBreakingData kBlockBreaking
        "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockBreakingTable.h")
kestrel_embed_file("${CMAKE_CURRENT_SOURCE_DIR}/data/icon.png" KestrelAppIconData kAppIconPng
        "${CMAKE_CURRENT_BINARY_DIR}/generated/AppIconPng.h")
kestrel_embed_file("${CMAKE_CURRENT_SOURCE_DIR}/data/kestrel.png" KestrelTitleData kTitlePng
        "${CMAKE_CURRENT_BINARY_DIR}/generated/TitlePng.h")

if(EXISTS "${KESTREL_FALCON_ROOT}/Falcon-BlockStateUpdater/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_FALCON_BLOCK_STATE_UPDATER "${KESTREL_FALCON_ROOT}/Falcon-BlockStateUpdater")
endif()
FetchContent_Declare(
    falcon_block_state_updater
    GIT_REPOSITORY https://github.com/Falcon-MC/BlockStateUpdater.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(falcon_block_state_updater)

file(GLOB KESTREL_UPGRADE_SCHEMAS CONFIGURE_DEPENDS "${falcon_block_state_updater_SOURCE_DIR}/Schemas/*.json")
list(SORT KESTREL_UPGRADE_SCHEMAS)
set(KESTREL_UPGRADE_SCHEMA_ARRAYS "")
set(KESTREL_UPGRADE_SCHEMA_ENTRIES "")
foreach(schema ${KESTREL_UPGRADE_SCHEMAS})
    get_filename_component(schemaName "${schema}" NAME)
    string(SUBSTRING "${schemaName}" 0 4 schemaId)
    file(READ "${schema}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
    string(APPEND KESTREL_UPGRADE_SCHEMA_ARRAYS "inline constexpr unsigned char kSchema${schemaId}[] = {\n${bytes}\n};\n")
    math(EXPR schemaNumber "1${schemaId} - 10000")
    string(APPEND KESTREL_UPGRADE_SCHEMA_ENTRIES "    { ${schemaNumber}, kSchema${schemaId}, sizeof(kSchema${schemaId}) },\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockUpgradeSchemas.h" CONTENT "#pragma once

#include <cstddef>
#include <cstdint>

namespace KestrelBlockUpgradeSchemas {

@KESTREL_UPGRADE_SCHEMA_ARRAYS@
struct Schema {
    int32_t id;
    const unsigned char* data;
    size_t size;
};

inline constexpr Schema kSchemas[] = {
@KESTREL_UPGRADE_SCHEMA_ENTRIES@};

}
" @ONLY)

find_package(ZLIB REQUIRED)

file(GLOB_RECURSE KESTREL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm
)
list(FILTER KESTREL_SOURCES EXCLUDE REGEX "/src/(platform/(win32|macos|linux)|render/(d3d12|metal|vulkan))/")

if(WIN32)
    file(GLOB_RECURSE KESTREL_PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/*.rc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/d3d12/*.cpp
    )
elseif(APPLE)
    file(GLOB_RECURSE KESTREL_PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/*.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/metal/*.mm
    )
else()
    file(GLOB_RECURSE KESTREL_PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/vulkan/*.cpp
    )

    find_package(Vulkan REQUIRED)

    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${CMAKE_COMMAND}
            -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/glfw-wayland-fullscreen-output.patch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/patches/ApplyPatch.cmake
        COMMAND ${CMAKE_COMMAND}
            -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/patches/glfw-wayland-window-icon.patch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/patches/ApplyPatch.cmake
    )
    FetchContent_MakeAvailable(glfw)
endif()

add_executable(Kestrel ${KESTREL_SOURCES} ${KESTREL_PLATFORM_SOURCES})

target_include_directories(Kestrel PRIVATE include ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(Kestrel PRIVATE FalconNetwork FalconBlockStateUpdater ZLIB::ZLIB)
option(KESTREL_BUILD_TESTS "Build Kestrel regression tests" OFF)
if(KESTREL_BUILD_TESTS)
    enable_testing()
    add_executable(KestrelInventoryDecodeTest tests/InventoryDecode.cpp)
    target_link_libraries(KestrelInventoryDecodeTest PRIVATE FalconProtocol)
    add_test(NAME inventory-decode COMMAND KestrelInventoryDecodeTest)
    add_executable(KestrelParticleEffectsTest
        tests/ParticleEffects.cpp
        src/world/particles/ParticleBinary.cpp
        src/world/particles/ParticleLibrary.cpp
        src/world/particles/ParticleSystem.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/world/animation/MolangScript.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelParticleEffectsTest PRIVATE include)
    target_link_libraries(KestrelParticleEffectsTest PRIVATE FalconProtocol ZLIB::ZLIB)
    add_test(NAME particle-effects COMMAND KestrelParticleEffectsTest)
endif()
option(KESTREL_BUILD_EXAMPLE_MODS "Build the example mods" OFF)
if(KESTREL_BUILD_EXAMPLE_MODS)
    add_subdirectory(examples/mods)
endif()
file(GLOB KESTREL_MOTION_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/client/motion/*.cpp)
if(MSVC)
    set_source_files_properties(${KESTREL_MOTION_SOURCES} PROPERTIES COMPILE_OPTIONS "/fp:strict")
else()
    set_source_files_properties(${KESTREL_MOTION_SOURCES} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
endif()

if(WIN32)
    target_compile_definitions(Kestrel PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(Kestrel PRIVATE d3d12 dxgi d3dcompiler dwmapi shell32 advapi32 comdlg32 ws2_32)
    set_target_properties(Kestrel PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_compile_options(Kestrel PRIVATE /utf-8 /W3)
    else()
        target_link_options(Kestrel PRIVATE -static)
    endif()
elseif(APPLE)
    target_link_libraries(Kestrel PRIVATE
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
    target_compile_options(Kestrel PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
else()
    target_link_libraries(Kestrel PRIVATE glfw Vulkan::Vulkan ${CMAKE_DL_LIBS} pthread m)
endif()
