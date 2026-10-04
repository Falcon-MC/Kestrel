cmake_minimum_required(VERSION 3.24)

project(Kestrel LANGUAGES CXX)

set(KESTREL_IOS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(KESTREL_IOS ON)
endif()

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
if(KESTREL_IOS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KestrelIosOpenSSL.cmake")
endif()

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
list(FILTER KESTREL_SOURCES EXCLUDE REGEX "/src/(platform/(win32|macos|linux|ios)|render/(d3d12|metal|vulkan))/")

if(WIN32)
    file(GLOB_RECURSE KESTREL_PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/*.rc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/d3d12/*.cpp
    )
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/Kestrel.rc
        PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/data/icon.ico)
elseif(KESTREL_IOS)
    list(FILTER KESTREL_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
    file(GLOB_RECURSE KESTREL_PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios/*.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/render/metal/*.mm
    )
    list(APPEND KESTREL_PLATFORM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/Gamepad.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/Library.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/System.mm
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

    # SDL3 gives us Wayland window icons and fullscreen on the window's own output out of the box.
    # Audio, rendering and the GPU API stay off, Kestrel brings its own.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "" FORCE)
    set(SDL_GPU OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

add_executable(Kestrel ${KESTREL_SOURCES} ${KESTREL_PLATFORM_SOURCES})

target_include_directories(Kestrel PRIVATE include ${stb_SOURCE_DIR} ${miniaudio_SOURCE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(NOT WIN32 AND NOT APPLE)
    find_program(KESTREL_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)
    set(KESTREL_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/render/vulkan")
    set(KESTREL_SHADER_HEADER "#pragma once\n#include <cstdint>\nnamespace kestrel::shaders {\n")
    function(kestrel_vulkan_shader symbol source)
        set(binary "${KESTREL_SHADER_DIRECTORY}/${symbol}.spv")
        set(header "${KESTREL_SHADER_DIRECTORY}/${symbol}.inc")
        add_custom_command(OUTPUT "${header}"
            BYPRODUCTS "${binary}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${KESTREL_SHADER_DIRECTORY}"
            COMMAND "${KESTREL_GLSLC}" -O ${ARGN} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}" -o "${binary}"
            COMMAND ${CMAKE_COMMAND} "-DINPUT=${binary}" "-DOUTPUT=${header}" "-DSYMBOL=${symbol}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/EmbedSpirv.cmake"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/EmbedSpirv.cmake"
            VERBATIM)
        set(KESTREL_SHADER_OUTPUTS ${KESTREL_SHADER_OUTPUTS} "${header}" PARENT_SCOPE)
        set(KESTREL_SHADER_HEADER "${KESTREL_SHADER_HEADER}#include \"${symbol}.inc\"\n" PARENT_SCOPE)
    endfunction()
    kestrel_vulkan_shader(UiVertex ui.vert)
    kestrel_vulkan_shader(UiFragment ui.frag)
    kestrel_vulkan_shader(WorldVertex world.vert)
    kestrel_vulkan_shader(WorldFragment world.frag)
    kestrel_vulkan_shader(BlendFragment world.frag -DBLEND)
    kestrel_vulkan_shader(ModelVertex model.vert)
    kestrel_vulkan_shader(ActorVertex actor.vert)
    kestrel_vulkan_shader(SkyVertex sky.vert)
    kestrel_vulkan_shader(SkyFragment sky.frag)
    kestrel_vulkan_shader(OverlayVertex model.vert -DOVERLAY)
    kestrel_vulkan_shader(OverlayFragment world.frag -DOVERLAY)
    kestrel_vulkan_shader(PrimitiveVertex primitive.vert)
    kestrel_vulkan_shader(PrimitiveFragment primitive.frag)
    file(GENERATE OUTPUT "${KESTREL_SHADER_DIRECTORY}/UiShaders.h" CONTENT "${KESTREL_SHADER_HEADER}}\n")
    add_custom_target(KestrelVulkanShaders DEPENDS ${KESTREL_SHADER_OUTPUTS})
    add_dependencies(Kestrel KestrelVulkanShaders)
    target_include_directories(Kestrel BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()
target_link_libraries(Kestrel PRIVATE FalconNetwork FalconBlockStateUpdater ZLIB::ZLIB)
option(KESTREL_BUILD_TESTS "Build Kestrel regression tests" OFF)
if(KESTREL_BUILD_TESTS)
    add_executable(KestrelLocalEffectsTest tests/LocalEffects.cpp src/modding/LocalEffects.cpp)
    target_include_directories(KestrelLocalEffectsTest PRIVATE include)
    add_test(NAME local-effects COMMAND KestrelLocalEffectsTest)

    add_executable(KestrelModUiTest tests/ModUi.cpp src/modding/ModUi.cpp src/modding/EventDispatcher.cpp)
    target_include_directories(KestrelModUiTest PRIVATE include)
    add_test(NAME mod-ui COMMAND KestrelModUiTest)

    add_executable(KestrelModSchedulerTest tests/ModScheduler.cpp src/modding/TaskScheduler.cpp)
    target_include_directories(KestrelModSchedulerTest PRIVATE include)
    target_link_libraries(KestrelModSchedulerTest PRIVATE Threads::Threads)
    add_test(NAME mod-scheduler COMMAND KestrelModSchedulerTest)

    add_executable(KestrelModDependenciesTest tests/ModDependencies.cpp src/modding/ModDependencies.cpp)
    target_include_directories(KestrelModDependenciesTest PRIVATE include)
    add_test(NAME mod-dependencies COMMAND KestrelModDependenciesTest)

    add_executable(KestrelModTexturesTest tests/ModTextures.cpp src/modding/TextureStore.cpp src/ui/Image.cpp)
    target_include_directories(KestrelModTexturesTest PRIVATE include ${stb_SOURCE_DIR})
    add_test(NAME mod-textures COMMAND KestrelModTexturesTest)

    add_executable(KestrelModTextureAtlasTest
        tests/ModTextureAtlas.cpp src/ui/Skin.cpp src/ui/GameAssets.cpp src/ui/Image.cpp
        src/world/packs/PackSource.cpp src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp src/util/Text.cpp src/client/diagnostics/DebugLog.cpp src/platform/Paths.cpp)
    target_include_directories(KestrelModTextureAtlasTest PRIVATE include ${stb_SOURCE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(KestrelModTextureAtlasTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME mod-texture-atlas COMMAND KestrelModTextureAtlasTest)
    set_tests_properties(mod-texture-atlas PROPERTIES ENVIRONMENT "KESTREL_VANILLA_PACK=${CMAKE_CURRENT_BINARY_DIR}/missing-mod-textures-assets")
    add_executable(KestrelJsonUiBindingsTest tests/JsonUiBindings.cpp src/ui/JsonUi.cpp src/ui/JsonUiExpression.cpp src/ui/JsonUiReader.cpp)
    target_include_directories(KestrelJsonUiBindingsTest PRIVATE include)
    target_link_libraries(KestrelJsonUiBindingsTest PRIVATE FalconProtocol)
    add_test(NAME json-ui-bindings COMMAND KestrelJsonUiBindingsTest)
    add_executable(KestrelUiClippingTest tests/UiClipping.cpp src/ui/DrawList.cpp)
    target_include_directories(KestrelUiClippingTest PRIVATE include)
    add_test(NAME ui-clipping COMMAND KestrelUiClippingTest)
    enable_testing()
    add_executable(KestrelAttachableFrameTest tests/AttachableFrame.cpp)
    target_include_directories(KestrelAttachableFrameTest PRIVATE include)
    add_test(NAME attachable-frame COMMAND KestrelAttachableFrameTest)
    add_executable(KestrelHandVisibilityTest tests/HandVisibility.cpp)
    target_include_directories(KestrelHandVisibilityTest PRIVATE include)
    add_test(NAME hand-visibility COMMAND KestrelHandVisibilityTest)
    add_executable(KestrelSettingsSliderTest tests/SettingsSlider.cpp)
    target_include_directories(KestrelSettingsSliderTest PRIVATE include)
    add_test(NAME settings-slider COMMAND KestrelSettingsSliderTest)
    add_executable(KestrelChatCommandsTest
        tests/ChatCommands.cpp
        src/menu/ChatCommands.cpp
        src/ui/Localization.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelChatCommandsTest PRIVATE include)
    target_link_libraries(KestrelChatCommandsTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME chat-commands COMMAND KestrelChatCommandsTest)
    add_executable(KestrelBorderBlockTest
        tests/BorderBlock.cpp
        src/world/assets/BlockClassification.cpp
        src/world/assets/BlockStates.cpp
        src/world/assets/TextureKeys.cpp
        src/world/assets/BlockShapes.cpp
        src/world/mesh/BlockModels.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelBorderBlockTest PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(KestrelBorderBlockTest PRIVATE FalconProtocol)
    add_test(NAME border-block COMMAND KestrelBorderBlockTest)
    add_executable(KestrelLocalizedTextTest
        tests/LocalizedText.cpp
        src/client/app/ChatText.cpp
        src/ui/Localization.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelLocalizedTextTest PRIVATE include)
    target_link_libraries(KestrelLocalizedTextTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME localized-text COMMAND KestrelLocalizedTextTest)
    add_executable(KestrelInventoryStackTest
        tests/InventoryStack.cpp
        src/client/inventory/InventoryStack.cpp
        src/world/blocks/ItemInfo.cpp
        src/ui/Localization.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelInventoryStackTest PRIVATE include)
    target_link_libraries(KestrelInventoryStackTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME inventory-stack COMMAND KestrelInventoryStackTest)
    add_executable(KestrelCreativeDropTest
        tests/CreativeDrop.cpp
        src/client/inventory/Inventory.cpp
        src/client/inventory/InventoryStack.cpp
        src/client/inventory/InventoryAnvil.cpp
        src/client/inventory/InventoryStations.cpp
        src/client/inventory/InventoryTrades.cpp
        src/client/inventory/ContainerLayout.cpp
        src/world/blocks/ItemInfo.cpp
        src/ui/Localization.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelCreativeDropTest PRIVATE include)
    target_link_libraries(KestrelCreativeDropTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME creative-drop COMMAND KestrelCreativeDropTest)
    add_executable(KestrelHotbarDropTest
        tests/HotbarDrop.cpp
        src/client/inventory/Inventory.cpp
        src/client/inventory/InventoryStack.cpp
        src/client/inventory/InventoryAnvil.cpp
        src/client/inventory/InventoryStations.cpp
        src/client/inventory/InventoryTrades.cpp
        src/client/inventory/ContainerLayout.cpp
        src/world/blocks/ItemInfo.cpp
        src/ui/Localization.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelHotbarDropTest PRIVATE include)
    target_link_libraries(KestrelHotbarDropTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME hotbar-drop COMMAND KestrelHotbarDropTest)
    add_executable(KestrelInventoryTransactionTest
        tests/InventoryTransaction.cpp
        src/client/inventory/Inventory.cpp
        src/client/inventory/InventoryStack.cpp
        src/client/inventory/InventoryAnvil.cpp
        src/client/inventory/InventoryStations.cpp
        src/client/inventory/InventoryTrades.cpp
        src/client/inventory/ContainerLayout.cpp
        src/world/blocks/ItemInfo.cpp
        src/ui/Localization.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelInventoryTransactionTest PRIVATE include)
    target_link_libraries(KestrelInventoryTransactionTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME inventory-transaction COMMAND KestrelInventoryTransactionTest)
    add_executable(KestrelHealthFeedbackTest tests/HealthFeedback.cpp)
    target_include_directories(KestrelHealthFeedbackTest PRIVATE include)
    add_test(NAME health-feedback COMMAND KestrelHealthFeedbackTest)
    add_executable(KestrelActorInterpolationTest tests/ActorInterpolation.cpp)
    target_include_directories(KestrelActorInterpolationTest PRIVATE include)
    add_test(NAME actor-interpolation COMMAND KestrelActorInterpolationTest)
    add_executable(KestrelRayBoxTest tests/RayBox.cpp)
    target_include_directories(KestrelRayBoxTest PRIVATE include)
    add_test(NAME actor-ray-box COMMAND KestrelRayBoxTest)
    add_executable(KestrelActorExtentTest tests/ActorExtent.cpp)
    target_include_directories(KestrelActorExtentTest PRIVATE include)
    add_test(NAME actor-extent COMMAND KestrelActorExtentTest)
    add_executable(KestrelActorRidersTest tests/ActorRiders.cpp)
    target_include_directories(KestrelActorRidersTest PRIVATE include)
    add_test(NAME actor-riders COMMAND KestrelActorRidersTest)
    add_executable(KestrelPlayerMotionTest
        tests/PlayerMotion.cpp
        src/client/motion/PlayerMotion.cpp
        src/client/motion/MotionMath.cpp
        src/client/motion/MotionWorld.cpp
        src/client/motion/MotionCollision.cpp
        src/client/motion/MotionLiquid.cpp
        src/world/blocks/BlockCollisions.cpp)
    target_include_directories(KestrelPlayerMotionTest PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME player-motion COMMAND KestrelPlayerMotionTest)
    add_executable(KestrelModCollisionTest tests/ModCollision.cpp src/world/blocks/BlockCollisions.cpp)
    target_include_directories(KestrelModCollisionTest PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_test(NAME mod-collision COMMAND KestrelModCollisionTest)
    add_executable(KestrelModWorldLiveTest
        tests/ModWorldLive.cpp
        src/world/chunks/ChunkStore.cpp
        src/world/chunks/SubChunk.cpp
        src/world/chunks/PalettedStorage.cpp
        src/world/chunks/ByteReader.cpp)
    target_include_directories(KestrelModWorldLiveTest PRIVATE include)
    target_link_libraries(KestrelModWorldLiveTest PRIVATE FalconProtocol Threads::Threads)
    add_test(NAME mod-world-live COMMAND KestrelModWorldLiveTest)
    add_executable(KestrelModBreakTest tests/ModBreak.cpp)
    target_include_directories(KestrelModBreakTest PRIVATE include)
    add_test(NAME mod-break COMMAND KestrelModBreakTest)
    add_executable(KestrelMeshPriorityTest tests/MeshPriority.cpp)
    target_include_directories(KestrelMeshPriorityTest PRIVATE include)
    add_test(NAME mesh-priority COMMAND KestrelMeshPriorityTest)
    find_package(Threads REQUIRED)
    add_executable(KestrelChunkDecodeTest
        tests/ChunkDecode.cpp
        src/world/chunks/WorldStream.cpp
        src/world/chunks/ChunkStore.cpp
        src/world/chunks/SubChunk.cpp
        src/world/chunks/PalettedStorage.cpp
        src/world/chunks/ByteReader.cpp)
    target_include_directories(KestrelChunkDecodeTest PRIVATE include)
    target_link_libraries(KestrelChunkDecodeTest PRIVATE FalconProtocol Threads::Threads)
    add_test(NAME chunk-decode COMMAND KestrelChunkDecodeTest)
    add_executable(KestrelInventoryDecodeTest tests/InventoryDecode.cpp)
    target_link_libraries(KestrelInventoryDecodeTest PRIVATE FalconProtocol)
    add_test(NAME inventory-decode COMMAND KestrelInventoryDecodeTest)
    add_executable(KestrelWorldClockTest tests/WorldClock.cpp)
    target_include_directories(KestrelWorldClockTest PRIVATE include)
    target_link_libraries(KestrelWorldClockTest PRIVATE FalconProtocol)
    add_test(NAME world-clock COMMAND KestrelWorldClockTest)
    add_executable(KestrelFogFrameTest tests/FogFrame.cpp src/client/view/Sky.cpp)
    target_include_directories(KestrelFogFrameTest PRIVATE include)
    add_test(NAME fog-frame COMMAND KestrelFogFrameTest)
    add_executable(KestrelEntityRenderingTest
        tests/EntityRendering.cpp
        src/world/animation/EntityAnimation.cpp
        src/world/animation/MolangScript.cpp
        src/world/mesh/Geometry.cpp
        src/world/blocks/ItemInfo.cpp
        src/ui/Localization.cpp
        src/world/packs/PackSource.cpp
        src/world/packs/ServerPack.cpp
        src/util/JsonText.cpp
        src/util/Text.cpp)
    target_include_directories(KestrelEntityRenderingTest PRIVATE include)
    target_link_libraries(KestrelEntityRenderingTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME entity-rendering COMMAND KestrelEntityRenderingTest)
    add_executable(KestrelCrystalAnchorTest tests/CrystalAnchor.cpp)
    target_include_directories(KestrelCrystalAnchorTest PRIVATE include)
    target_link_libraries(KestrelCrystalAnchorTest PRIVATE FalconProtocol)
    add_test(NAME crystal-anchor COMMAND KestrelCrystalAnchorTest)
    if(WIN32)
        add_executable(KestrelD3D12ShadersTest tests/D3D12Shaders.cpp)
        target_include_directories(KestrelD3D12ShadersTest PRIVATE include)
        target_link_libraries(KestrelD3D12ShadersTest PRIVATE d3dcompiler)
        add_test(NAME d3d12-shaders COMMAND KestrelD3D12ShadersTest)
        add_executable(KestrelTerrainTintTest tests/TerrainTint.cpp)
        target_include_directories(KestrelTerrainTintTest PRIVATE include)
        target_link_libraries(KestrelTerrainTintTest PRIVATE d3d11 d3dcompiler)
        add_test(NAME terrain-tint COMMAND KestrelTerrainTintTest)
    endif()
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
    target_link_libraries(KestrelParticleEffectsTest PRIVATE FalconNetwork ZLIB::ZLIB)
    add_test(NAME particle-effects COMMAND KestrelParticleEffectsTest)
    add_test(NAME particle-explosion-lifetime COMMAND KestrelParticleEffectsTest --explosion-lifetime)
    add_test(NAME particle-handles COMMAND KestrelParticleEffectsTest --tracked-effects)
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
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework GameController"
        "-framework CoreFoundation"
    )
    if(KESTREL_IOS)
        set_source_files_properties(src/audio/SoundEngine.cpp PROPERTIES LANGUAGE OBJCXX)
        target_link_libraries(Kestrel PRIVATE "-framework UIKit" "-framework Foundation" "-framework CoreGraphics" "-framework AVFAudio" "-framework AVFoundation")
        target_compile_definitions(Kestrel PRIVATE KESTREL_IOS=1)
        set_target_properties(Kestrel PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios/Info.plist.in"
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.falconmc.kestrel"
            MACOSX_BUNDLE_BUNDLE_NAME "Kestrel"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            MACOSX_BUNDLE_BUNDLE_VERSION "1"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        )
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KestrelIos.cmake")
    else()
        target_link_libraries(Kestrel PRIVATE "-framework Cocoa")
    endif()
    target_compile_options(Kestrel PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
else()
    target_link_libraries(Kestrel PRIVATE SDL3::SDL3-static Vulkan::Vulkan ${CMAKE_DL_LIBS} pthread m)
endif()
