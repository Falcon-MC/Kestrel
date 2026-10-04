include(ExternalProject)
find_package(Perl REQUIRED)
find_program(KESTREL_MAKE NAMES make REQUIRED)

FetchContent_Declare(openssl
    URL https://github.com/openssl/openssl/releases/download/openssl-3.5.8/openssl-3.5.8.tar.gz
    URL_HASH SHA256=a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR cmake-headers-only
)
FetchContent_MakeAvailable(openssl)
set(KESTREL_OPENSSL_ROOT "${openssl_BINARY_DIR}/install")
file(MAKE_DIRECTORY "${KESTREL_OPENSSL_ROOT}/include" "${KESTREL_OPENSSL_ROOT}/lib")

if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(openssl_platform iossimulator-arm64-xcrun)
    set(openssl_minimum "-mios-simulator-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
    set(openssl_platform ios64-xcrun)
    set(openssl_minimum "-miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
ExternalProject_Add(KestrelOpenSSL
    SOURCE_DIR "${openssl_SOURCE_DIR}"
    BINARY_DIR "${openssl_BINARY_DIR}/build"
    PREFIX "${openssl_BINARY_DIR}/project"
    DOWNLOAD_COMMAND ""
    CONFIGURE_COMMAND "${PERL_EXECUTABLE}" "${openssl_SOURCE_DIR}/Configure"
        ${openssl_platform} no-shared no-tests no-apps no-module no-dso
        "${openssl_minimum}" "--prefix=${KESTREL_OPENSSL_ROOT}" --libdir=lib
    BUILD_COMMAND "${KESTREL_MAKE}" -j8
    INSTALL_COMMAND "${KESTREL_MAKE}" install_sw
    BUILD_BYPRODUCTS "${KESTREL_OPENSSL_ROOT}/lib/libssl.a" "${KESTREL_OPENSSL_ROOT}/lib/libcrypto.a"
    LOG_CONFIGURE TRUE
    LOG_BUILD TRUE
    LOG_INSTALL TRUE
)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
