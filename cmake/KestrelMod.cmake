# Helpers for building Kestrel mods. Include this file from a mod's own
# CMakeLists.txt, or use it in tree through examples/mods.
#
#   kestrel_add_mod(my_mod src/MyMod.cpp)
#   kestrel_mod_spirv(my_mod shaders/effect.vert shaders/effect.frag)
#
# The library comes out without a lib prefix, ready to drop into the mods
# folder. Build it with the same compiler family as Kestrel.

set(KESTREL_MOD_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../include" CACHE PATH "Kestrel's include directory")

function(kestrel_add_mod target)
    add_library(${target} MODULE ${ARGN})
    target_include_directories(${target} PRIVATE "${KESTREL_MOD_INCLUDE_DIR}")
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

# Compiles GLSL to SPIR-V with glslc and makes <file>.spv.inc next to the
# generated sources: comma separated words for a std::vector<uint32_t>
# initializer, so the shader ships inside the mod. Needs the Vulkan SDK.
function(kestrel_mod_spirv target)
    find_program(KESTREL_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)
    set(outputDir "${CMAKE_CURRENT_BINARY_DIR}/${target}_spirv")
    file(MAKE_DIRECTORY "${outputDir}")
    set(outputs "")
    foreach(shader ${ARGN})
        get_filename_component(source "${shader}" ABSOLUTE)
        get_filename_component(name "${shader}" NAME)
        set(output "${outputDir}/${name}.spv.inc")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${KESTREL_GLSLC}" -mfmt=num -o "${output}" "${source}"
            DEPENDS "${source}"
            COMMENT "Compiling ${name} to SPIR-V"
            VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()
    target_sources(${target} PRIVATE ${outputs})
    target_include_directories(${target} PRIVATE "${outputDir}")
endfunction()
