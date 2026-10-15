cmake_minimum_required(VERSION 3.10)
project(avb_verify C)

# Try system-installed libavb first, fall back to submodule
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBAVB QUIET libavb)
endif()

add_executable(avb_verify avb_verify.c)
target_compile_options(avb_verify PRIVATE
    -Wall -Wextra
    -D_FORTIFY_SOURCE=2
    -fstack-protector-strong
    -fPIE
    -Wformat -Wformat-security
    -Werror=format-security
)
target_link_options(avb_verify PRIVATE
    -pie
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
)

if(LIBAVB_FOUND)
    message(STATUS "Using system libavb: ${LIBAVB_LINK_LIBRARIES}")
    target_include_directories(avb_verify PRIVATE ${LIBAVB_INCLUDE_DIRS})
    target_link_libraries(avb_verify PRIVATE ${LIBAVB_LINK_LIBRARIES})
else()
    message(STATUS "System libavb not found, using avb submodule")
    set(LIBAVB_DIR avb/libavb)
    target_sources(avb_verify PRIVATE
        ${LIBAVB_DIR}/avb_chain_partition_descriptor.c
        ${LIBAVB_DIR}/avb_cmdline.c
        ${LIBAVB_DIR}/avb_crc32.c
        ${LIBAVB_DIR}/avb_crypto.c
        ${LIBAVB_DIR}/avb_descriptor.c
        ${LIBAVB_DIR}/avb_footer.c
        ${LIBAVB_DIR}/avb_hash_descriptor.c
        ${LIBAVB_DIR}/avb_hashtree_descriptor.c
        ${LIBAVB_DIR}/avb_kernel_cmdline_descriptor.c
        ${LIBAVB_DIR}/avb_mldsa.c
        ${LIBAVB_DIR}/avb_property_descriptor.c
        ${LIBAVB_DIR}/avb_rsa.c
        ${LIBAVB_DIR}/avb_slot_verify.c
        ${LIBAVB_DIR}/avb_sysdeps_posix.c
        ${LIBAVB_DIR}/avb_util.c
        ${LIBAVB_DIR}/avb_vbmeta_image.c
        ${LIBAVB_DIR}/avb_version.c
        ${LIBAVB_DIR}/crypto/sha256_impl.c
        ${LIBAVB_DIR}/crypto/sha512_impl.c
        ${LIBAVB_DIR}/crypto/mldsa_impl.c
    )
    target_compile_definitions(avb_verify PRIVATE AVB_COMPILATION)
    target_include_directories(avb_verify PRIVATE ${CMAKE_SOURCE_DIR}/avb ${LIBAVB_DIR}/crypto)
endif()

install(TARGETS avb_verify RUNTIME DESTINATION bin)

option(INSTALL_AVB_SIGN "Install avb_sign.py host signing script and avbtool" OFF)
if(INSTALL_AVB_SIGN)
    install(PROGRAMS avb_sign.py DESTINATION bin RENAME avb_sign)
    if(EXISTS ${CMAKE_SOURCE_DIR}/avb/avbtool.py)
        install(PROGRAMS avb/avbtool.py DESTINATION bin RENAME avbtool)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME integration COMMAND ${CMAKE_SOURCE_DIR}/avb_verify_test.sh
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(integration PROPERTIES
        ENVIRONMENT "avb_verify=$<TARGET_FILE:avb_verify>")

    add_test(NAME avb_sign COMMAND ${CMAKE_SOURCE_DIR}/avb_sign_test.sh
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(avb_sign PROPERTIES
        ENVIRONMENT "avb_verify=$<TARGET_FILE:avb_verify>")

    # Small internal tests share the verifier's exact libavb configuration.
    get_target_property(VERIFY_SOURCES avb_verify SOURCES)
    list(REMOVE_ITEM VERIFY_SOURCES avb_verify.c)
    add_executable(avb_verify_unit tests/verify_unit.c ${VERIFY_SOURCES})
    foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_LIBRARIES)
        get_target_property(value avb_verify ${prop})
        if(value)
            set_property(TARGET avb_verify_unit PROPERTY ${prop} ${value})
        endif()
    endforeach()
    target_compile_options(avb_verify_unit PRIVATE -Wall -Wextra)
    add_test(NAME verify_unit COMMAND avb_verify_unit)

    find_program(PYTHON3_EXECUTABLE NAMES python3)
    if(NOT PYTHON3_EXECUTABLE)
        message(FATAL_ERROR "Python 3 is required for tests; use -DBUILD_TESTING=OFF for target-only builds")
    endif()
    add_test(NAME large_images COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/large_images.py)
    set_tests_properties(large_images PROPERTIES
        ENVIRONMENT "AVB_VERIFY=$<TARGET_FILE:avb_verify>"
        TIMEOUT 300)
    add_test(NAME build32 COMMAND sh ${CMAKE_SOURCE_DIR}/tests/build32.sh)
    set_tests_properties(build32 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
