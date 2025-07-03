cmake_minimum_required(VERSION 3.30)

if (NOT CMAKE_TOOLCHAIN_FILE MATCHES "toolchain-nxdk.cmake")
    message(FATAL_ERROR "This project must be built with the nxdk toolchain (`-DCMAKE_TOOLCHAIN_FILE=<YOUR_NXDK_DIR>/share/toolchain-nxdk.cmake`). Your toolchain is ${CMAKE_TOOLCHAIN_FILE}")
endif ()

project(pbkitplusplus VERSION 0.5.0 LANGUAGES CXX)

option(
        NO_OPT
        "Suppress optimization on core files that are typically optimized regardless of release/debug configuration"
        OFF
)

block(SCOPE_FOR VARIABLES)
    get_filename_component(NXDK_ROOT_DIR "${CMAKE_TOOLCHAIN_FILE}/../.." ABSOLUTE)
    set(NXDK_DIR "${NXDK_ROOT_DIR}" CACHE PATH "Path to the nxdk root directory.")

    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")
    include(PrebuildNXDK)

    find_package(NXDK REQUIRED)
    find_package(NXDK_SDL2 REQUIRED)
    find_package(NXDK_SDL2_Image REQUIRED)


    include(FetchContent)

    find_package(XboxMath QUIET)
    if (XboxMath_FOUND)
        message(STATUS "Using installed XboxMath version ${XboxMath_VERSION}")
    else ()
        FetchContent_Declare(
                XboxMath_fetched
                GIT_REPOSITORY https://github.com/abaire/xbox_math3d.git
                GIT_TAG main
        )
        FetchContent_MakeAvailable(XboxMath_fetched)
    endif ()
    if (TARGET xbox_math3d AND NOT TARGET XboxMath::xbox_math3d)
        add_library(XboxMath::xbox_math3d ALIAS xbox_math3d)
    endif ()

    find_package(XboxSwizzle QUIET)
    if (XboxSwizzle_FOUND)
        message(STATUS "Using installed XboxSwizzle version ${XboxSwizzle_VERSION}")
    else ()
        FetchContent_Declare(
                XboxSwizzle_fetched
                GIT_REPOSITORY https://github.com/abaire/xbox-swizzle.git
                GIT_TAG main
        )
        FetchContent_MakeAvailable(XboxSwizzle_fetched)
    endif ()
    if (TARGET xbox-swizzle AND NOT TARGET XboxSwizzle::xbox-swizzle)
        add_library(XboxSwizzle::xbox-swizzle ALIAS xbox-swizzle)
    endif ()

    # Vertex shaders.
    include(NV2A_VSH REQUIRED)
    generate_nv2a_vshinc_files(
            OUTPUT_VARIABLE _SHADER_FILES
            INCLUDE_DIRECTORIES_VARIABLE _SHADER_INCLUDE_DIRS
            GENERATION_TARGET_VARIABLE _SHADER_GEN_TARGET
            SOURCES
            src/shaders/passthrough.vsh
    )

    set(
            _PUBLIC_HEADERS
            src/dds_image.h
            src/models/model_builder.h
            src/light.h
            src/pushbuffer.h
            src/nv2astate.h
            src/shaders/orthographic_vertex_shader.h
            src/shaders/passthrough_vertex_shader.h
            src/shaders/perspective_vertex_shader.h
            src/shaders/projection_vertex_shader.h
            src/shaders/vertex_shader_program.h
            src/texture_format.h
            src/texture_generator.h
            src/texture_stage.h
            src/vertex_buffer.h
    )

    add_library(
            ${PROJECT_NAME}
            STATIC
            src/dds_image.cpp
            src/models/model_builder.cpp
            src/light.cpp
            src/pushbuffer.cpp
            src/nv2astate.cpp
            src/shaders/orthographic_vertex_shader.cpp
            src/shaders/passthrough_vertex_shader.cpp
            src/shaders/perspective_vertex_shader.cpp
            src/shaders/projection_vertex_shader.cpp
            src/shaders/vertex_shader_program.cpp
            src/texture_format.cpp
            src/texture_generator.cpp
            src/texture_stage.cpp
            src/vertex_buffer.cpp
            src/pbkpp_assert.cpp
            src/pbkpp_assert.h
            ${_PUBLIC_HEADERS}
            ${_SHADER_FILES}
    )

    add_dependencies(
            ${PROJECT_NAME}
            ${_SHADER_GEN_TARGET}
    )

    target_include_directories(
            ${PROJECT_NAME}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
            PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}
            ${_SHADER_INCLUDE_DIRS}
    )

    target_compile_definitions(
            ${PROJECT_NAME}
            PRIVATE
            _USE_MATH_DEFINES
    )

    if (NO_OPT)
        target_compile_options(
                ${PROJECT_NAME}
                PRIVATE
                -g
                -gdwarf-4
                -O0
                -Wall
                -fstandalone-debug
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-builtin-macro-redefined>   # Suppress warning from NXDK undef of __STDC_NO_THREADS__
        )
        target_link_options(
                ${PROJECT_NAME}
                PRIVATE
                "/debug:full"
        )
    else ()
        target_compile_options(
                ${PROJECT_NAME}
                PRIVATE
                -O3
                -fno-strict-aliasing
                -Wall
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-builtin-macro-redefined>   # Suppress warning from NXDK undef of __STDC_NO_THREADS__
        )
        target_link_options(
                ${PROJECT_NAME}
                PRIVATE
                "/debug:none"
        )
    endif ()

    set_property(
            TARGET
            ${PROJECT_NAME}
            PROPERTY
            PUBLIC_HEADER ${_PUBLIC_HEADERS}
    )

    target_link_libraries(
            ${PROJECT_NAME}
            PUBLIC
            XboxSwizzle::xbox-swizzle
            XboxMath::xbox_math3d
            PRIVATE
            NXDK::NXDK
            NXDK::NXDK_CXX
            NXDK::SDL2
            NXDK::SDL2_Image
    )

    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(
            TARGETS
            ${PROJECT_NAME}
            EXPORT PbkitPlusPlusTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pbkitplusplus
    )

    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/PbkitPlusPlusConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY AnyNewerVersion
    )

    configure_package_config_file(
            "cmake/PbkitPlusPlusConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/PbkitPlusPlusConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )

    install(
            EXPORT PbkitPlusPlusTargets
            FILE PbkitPlusPlusTargets.cmake
            NAMESPACE PbkitPlusPlus::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )

    install(
            FILES
            "${CMAKE_CURRENT_BINARY_DIR}/PbkitPlusPlusConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/PbkitPlusPlusConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )

    add_subdirectory(sample EXCLUDE_FROM_ALL)

endblock()
