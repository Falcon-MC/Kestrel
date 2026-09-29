include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/KestrelMod.cmake")

kestrel_add_mod(hello hello/HelloMod.cpp)

kestrel_add_mod(shader_demo shader_demo/ShaderDemoMod.cpp)
find_program(KESTREL_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin")
if(NOT WIN32 AND NOT APPLE AND NOT KESTREL_GLSLC)
    message(WARNING "glslc not found, shader_demo builds without its Vulkan shaders")
elseif(NOT WIN32 AND NOT APPLE)
    kestrel_mod_spirv(shader_demo
        shader_demo/shaders/quad.vert shader_demo/shaders/vignette.frag shader_demo/shaders/glow.frag)
    target_compile_definitions(shader_demo PRIVATE SHADER_DEMO_SPIRV)
endif()
