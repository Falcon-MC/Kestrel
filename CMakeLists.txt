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

set(FALCON_BEDROCK_DATA_PROTOCOL 2193)
include("${KESTREL_FALCON_ROOT}/Falcon-BedrockData/cmake/FalconBedrockData.cmake")
falcon_bedrock_data_embed_binary(FILE block_palette.nbt NAMESPACE KestrelBlockPaletteData
        SYMBOL kBlockPaletteNbt OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockPaletteNbt.h")
falcon_bedrock_data_embed_binary(FILE block_definitions.nbt NAMESPACE KestrelBlockDefinitionData
        SYMBOL kBlockDefinitionsNbt OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/BlockDefinitionsNbt.h")

find_package(ZLIB REQUIRED)

file(GLOB_RECURSE KESTREL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm
)
list(FILTER KESTREL_SOURCES EXCLUDE REGEX "/src/(platform/(win32|macos|linux)|render/(d3d12|metal|vulkan))/")

if(WIN32)
    file(GLOB_RECURSE KESTREL_PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/*.cpp
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
    )
    FetchContent_MakeAvailable(glfw)
endif()

add_executable(Kestrel ${KESTREL_SOURCES} ${KESTREL_PLATFORM_SOURCES})

target_include_directories(Kestrel PRIVATE include ${stb_SOURCE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(Kestrel PRIVATE FalconNetwork ZLIB::ZLIB)

if(WIN32)
    target_compile_definitions(Kestrel PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(Kestrel PRIVATE d3d12 dxgi d3dcompiler dwmapi shell32)
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
    )
    target_compile_options(Kestrel PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
else()
    target_link_libraries(Kestrel PRIVATE glfw Vulkan::Vulkan)
endif()
