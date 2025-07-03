cmake_minimum_required(VERSION 3.30)

if (NOT CMAKE_TOOLCHAIN_FILE MATCHES "toolchain-nxdk.cmake")
    message(FATAL_ERROR "This project must be built with the nxdk toolchain (`-DCMAKE_TOOLCHAIN_FILE=<YOUR_NXDK_DIR>/share/toolchain-nxdk.cmake`)")
endif ()

set(XBE_TITLE "pbkitplusplus sample")
project(pbkitplusplus-sample)

block(SCOPE_FOR VARIABLES)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")
    include(PrebuildNXDK)

    include(PBKPP_XBEUtils REQUIRED)
    find_package(NXDK REQUIRED)
    find_package(NXDK_SDL2 REQUIRED)
    find_package(NXDK_SDL2_Image REQUIRED)

    add_executable(
            ${PROJECT_NAME}
            src/main.cpp
            src/torus_model.cpp
            src/torus_model.h
    )

    target_link_libraries(
            ${PROJECT_NAME}
            PRIVATE
            pbkitplusplus
    )

    # Pull debug info out of the binary into a host-side linked binary.
    pbkpp_split_debug(${PROJECT_NAME})

    set(EXECUTABLE_BINARY "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.exe")

    macro(set_opt_compile_and_link_options TARGET_NAME)
        target_compile_options(
                "${TARGET_NAME}"
                PRIVATE
                -O3
                -fno-strict-aliasing
                -Wall
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-builtin-macro-redefined>   # Suppress warning from NXDK undef of __STDC_NO_THREADS__
                -D_USE_MATH_DEFINES
        )
        target_link_options(
                "${TARGET_NAME}"
                PRIVATE
                "/debug:none"
        )
    endmacro()

    macro(set_compile_and_link_options TARGET_NAME)
        if (CMAKE_BUILD_TYPE MATCHES Debug)
            target_compile_options(
                    "${TARGET_NAME}"
                    PRIVATE
                    -g
                    -gdwarf-4
                    -O0
                    -Wall
                    -fstandalone-debug
                    $<$<COMPILE_LANGUAGE:CXX>:-Wno-builtin-macro-redefined>   # Suppress warning from NXDK undef of __STDC_NO_THREADS__
                    -D_USE_MATH_DEFINES
            )
            target_link_options(
                    "${TARGET_NAME}"
                    PRIVATE
                    "/debug:full"
            )
        else ()
            set_opt_compile_and_link_options("${TARGET_NAME}")
        endif ()
    endmacro()

    set_compile_and_link_options(${PROJECT_NAME})

    target_include_directories(
            ${PROJECT_NAME}
            PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}"
    )

    target_link_libraries(
            ${PROJECT_NAME}
            PUBLIC
            generated_nv2a_vertex_shaders
            PRIVATE
            NXDK::NXDK
            NXDK::NXDK_CXX
            NXDK::SDL2
            NXDK::SDL2_Image
    )

    pbkpp_add_xbe(
            xbe_file "${EXECUTABLE_BINARY}"
            TITLE "${XBE_TITLE}"
            RESOURCE_ROOTS
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            RESOURCE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    )

    pbkpp_add_xiso(${PROJECT_NAME}_xiso xbe_file)

endblock()
