set(KESTREL_IOS_RESOURCE_PACKS "" CACHE PATH "Minecraft resource_packs directory to bundle for iOS")
set(KESTREL_IOS_CA_BUNDLE "" CACHE FILEPATH "PEM certificate authorities for iOS TLS")
if(NOT IS_DIRECTORY "${KESTREL_IOS_RESOURCE_PACKS}/vanilla")
    message(FATAL_ERROR "Set KESTREL_IOS_RESOURCE_PACKS to a resource_packs directory containing vanilla")
endif()
if(NOT EXISTS "${KESTREL_IOS_CA_BUNDLE}")
    message(FATAL_ERROR "Set KESTREL_IOS_CA_BUNDLE to a PEM certificate authority bundle")
endif()
add_custom_command(TARGET Kestrel POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE=${KESTREL_IOS_RESOURCE_PACKS}"
        "-DDESTINATION=$<TARGET_BUNDLE_DIR:Kestrel>/resource_packs"
        "-DCA_BUNDLE=${KESTREL_IOS_CA_BUNDLE}"
        "-DBUNDLE=$<TARGET_BUNDLE_DIR:Kestrel>"
        "-DICON=${CMAKE_CURRENT_SOURCE_DIR}/data/icon.png"
        "-DTOUCH_UI=${CMAKE_CURRENT_SOURCE_DIR}/data/ui/touch_controls.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyIosResources.cmake"
    VERBATIM)
add_custom_target(KestrelIpa
    COMMAND ${CMAKE_COMMAND}
        "-DAPP=$<TARGET_BUNDLE_DIR:Kestrel>"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/Kestrel.ipa"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageIos.cmake"
    DEPENDS Kestrel
    VERBATIM)
