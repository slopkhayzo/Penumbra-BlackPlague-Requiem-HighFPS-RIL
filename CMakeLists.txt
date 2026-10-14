cmake_minimum_required(VERSION 3.24)

project(
    PenumbraBlackPlagueFpsFix
    VERSION 1.0.0
    DESCRIPTION "High-frame-rate presentation fix for Penumbra: Black Plague and Requiem"
    LANGUAGES C CXX
)

include(CTest)

# Appended to the version in the log banner and the package names, for
# example "-rc1". Empty for a final release.
set(PBPFPS_VERSION_SUFFIX "-rc1" CACHE STRING "Pre-release suffix for this build")
set(PBPFPS_FULL_VERSION "${PROJECT_VERSION}${PBPFPS_VERSION_SUFFIX}")

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "PenumbraBlackPlague.FPSFix supports 32-bit Windows/MSVC only")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure an x86 build environment; Penumbra.exe and Requiem.exe are PE32/x86")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
add_subdirectory(third_party/minhook EXCLUDE_FROM_ALL)
set_property(TARGET minhook PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_library(PenumbraBlackPlagueFpsFix MODULE
    src/bootstrap.cpp
    src/camera_interpolation.cpp
    src/camera_runtime.cpp
    src/config.cpp
    src/dllmain.cpp
    src/entity_root_runtime.cpp
    src/frame_cadence.cpp
    src/gui_cursor_runtime.cpp
    src/hands_runtime.cpp
    src/host_validation.cpp
    src/image_trail_runtime.cpp
    src/interaction_cursor_runtime.cpp
    src/logger.cpp
    src/mesh_cache.cpp
    src/motion_blur_runtime.cpp
    src/mouse_preview_math.cpp
    src/particle_runtime.cpp
    src/pattern_scan.cpp
    src/pose_interpolation.cpp
    src/pose_runtime.cpp
    src/presentation_clock.cpp
    src/post_effect_timing.cpp
    src/rigid_transform.cpp
    src/target_black_plague.cpp
    src/target_host.cpp
    src/target_layout.cpp
    src/target_mapping.cpp
    src/target_requiem.cpp
    src/telemetry.cpp
    src/world_runtime.cpp
)

target_include_directories(PenumbraBlackPlagueFpsFix PRIVATE src)
target_compile_features(PenumbraBlackPlagueFpsFix PRIVATE cxx_std_20)
target_compile_definitions(PenumbraBlackPlagueFpsFix PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    PENUMBRA_BLACK_PLAGUE_FPS_FIX_VERSION=L"${PBPFPS_FULL_VERSION}"
)
target_compile_options(PenumbraBlackPlagueFpsFix PRIVATE
    /W4
    /WX
    /permissive-
    /utf-8
    /Zc:preprocessor
)
target_link_options(PenumbraBlackPlagueFpsFix PRIVATE
    /DYNAMICBASE
    /NXCOMPAT
    /GUARD:CF
    /INCREMENTAL:NO
)
target_link_libraries(PenumbraBlackPlagueFpsFix PRIVATE minhook)
set_property(TARGET PenumbraBlackPlagueFpsFix PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
set_target_properties(PenumbraBlackPlagueFpsFix PROPERTIES
    OUTPUT_NAME "PenumbraBlackPlague.FPSFix"
    PREFIX ""
    SUFFIX ".asi"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET PenumbraBlackPlagueFpsFix POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/PenumbraBlackPlague.FPSFix.ini"
            "$<TARGET_FILE_DIR:PenumbraBlackPlagueFpsFix>/PenumbraBlackPlague.FPSFix.ini"
    VERBATIM
)

install(TARGETS PenumbraBlackPlagueFpsFix LIBRARY DESTINATION . RUNTIME DESTINATION .)

# Release archives: cmake --build <dir> --target dist
# The loader bundle is produced only when the pinned Ultimate ASI Loader DLL
# is available at PBPFPS_LOADER_DLL.
set(PBPFPS_LOADER_DLL
    "${CMAKE_SOURCE_DIR}/research/tools/ultimate-asi-loader/dinput8.dll"
    CACHE FILEPATH "Ultimate ASI Loader x86 DLL to bundle (optional)")
add_custom_target(dist
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/packaging/package.ps1"
            -Version "${PBPFPS_FULL_VERSION}"
            -BinDir "$<TARGET_FILE_DIR:PenumbraBlackPlagueFpsFix>"
            -OutDir "${CMAKE_SOURCE_DIR}/dist"
            -LoaderDll "${PBPFPS_LOADER_DLL}"
    DEPENDS PenumbraBlackPlagueFpsFix
    VERBATIM
)
install(FILES config/PenumbraBlackPlague.FPSFix.ini DESTINATION .)

if(BUILD_TESTING)
    # Optional: the directory holding the retail Penumbra.exe and Requiem.exe.
    # When set, the adapter test also maps both files and checks that each one
    # selects its own adapter. Nothing is executed or modified.
    set(PBPFPS_GAME_DIR "" CACHE PATH
        "Directory containing Penumbra.exe and Requiem.exe (optional, tests only)")

    # pbpfps_add_test(<name> [LAYOUT] [WINDOWS] [SOURCES <file>...])
    #   LAYOUT   links the layout table and the Overture-values test fixture
    #   WINDOWS  defines the lean Windows macros for tests that include Windows.h
    function(pbpfps_add_test name)
        cmake_parse_arguments(ARG "LAYOUT;WINDOWS" "" "SOURCES" ${ARGN})
        add_executable(${name}_tests tests/${name}_tests.cpp ${ARG_SOURCES})
        if(ARG_LAYOUT)
            target_sources(${name}_tests PRIVATE
                src/target_layout.cpp
                tests/layout_fixture.cpp
            )
        endif()
        target_include_directories(${name}_tests PRIVATE src)
        target_compile_features(${name}_tests PRIVATE cxx_std_20)
        if(ARG_WINDOWS OR ARG_LAYOUT)
            target_compile_definitions(${name}_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        endif()
        target_compile_options(${name}_tests PRIVATE /W4 /WX /permissive- /utf-8)
        set_property(TARGET ${name}_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        add_test(NAME ${name} COMMAND ${name}_tests)
    endfunction()

    pbpfps_add_test(frame_cadence SOURCES src/frame_cadence.cpp)
    pbpfps_add_test(guarded_memory WINDOWS)
    pbpfps_add_test(presentation_clock SOURCES src/presentation_clock.cpp)
    pbpfps_add_test(camera_interpolation SOURCES src/camera_interpolation.cpp)
    pbpfps_add_test(camera_runtime LAYOUT SOURCES
        src/camera_runtime.cpp src/camera_interpolation.cpp)
    pbpfps_add_test(world_runtime LAYOUT SOURCES
        src/world_runtime.cpp src/rigid_transform.cpp)
    pbpfps_add_test(hands_runtime LAYOUT SOURCES
        src/hands_runtime.cpp src/rigid_transform.cpp)
    pbpfps_add_test(entity_root_runtime LAYOUT SOURCES
        src/entity_root_runtime.cpp src/rigid_transform.cpp)
    pbpfps_add_test(pose_interpolation SOURCES
        src/pose_interpolation.cpp src/rigid_transform.cpp)
    pbpfps_add_test(pose_runtime LAYOUT SOURCES
        src/pose_runtime.cpp src/pose_interpolation.cpp src/rigid_transform.cpp)
    pbpfps_add_test(mesh_cache LAYOUT SOURCES src/mesh_cache.cpp)
    pbpfps_add_test(post_effect_timing SOURCES src/post_effect_timing.cpp)
    pbpfps_add_test(image_trail_runtime LAYOUT SOURCES
        src/image_trail_runtime.cpp src/post_effect_timing.cpp)
    pbpfps_add_test(particle_runtime LAYOUT SOURCES src/particle_runtime.cpp)
    pbpfps_add_test(motion_blur_runtime LAYOUT SOURCES
        src/motion_blur_runtime.cpp src/post_effect_timing.cpp)
    pbpfps_add_test(mouse_preview_math SOURCES src/mouse_preview_math.cpp)
    pbpfps_add_test(interaction_cursor_runtime LAYOUT SOURCES
        src/interaction_cursor_runtime.cpp)
    pbpfps_add_test(gui_cursor_runtime LAYOUT SOURCES
        src/gui_cursor_runtime.cpp src/mouse_preview_math.cpp)
    pbpfps_add_test(host_validation WINDOWS SOURCES src/host_validation.cpp)
    pbpfps_add_test(config WINDOWS SOURCES src/config.cpp)
    pbpfps_add_test(pattern_scan SOURCES src/pattern_scan.cpp)

    # Host selection: synthetic images always; the installed retail
    # executables too when PBPFPS_GAME_DIR points at them.
    add_executable(target_host_tests
        tests/target_host_tests.cpp
        src/pattern_scan.cpp
        src/target_black_plague.cpp
        src/target_host.cpp
        src/target_mapping.cpp
        src/target_requiem.cpp
    )
    target_include_directories(target_host_tests PRIVATE src)
    target_compile_features(target_host_tests PRIVATE cxx_std_20)
    target_compile_definitions(target_host_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(target_host_tests PRIVATE /W4 /WX /permissive- /utf-8)
    set_property(TARGET target_host_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    if(PBPFPS_GAME_DIR AND EXISTS "${PBPFPS_GAME_DIR}/Penumbra.exe"
       AND EXISTS "${PBPFPS_GAME_DIR}/Requiem.exe")
        file(TO_NATIVE_PATH "${PBPFPS_GAME_DIR}" PBPFPS_GAME_DIR_NATIVE)
        add_test(NAME target_host
            COMMAND target_host_tests "${PBPFPS_GAME_DIR_NATIVE}")
    else()
        add_test(NAME target_host COMMAND target_host_tests)
    endif()

    # Presentation paths must not issue page queries; see guarded_memory.h.
    add_test(NAME no_page_queries
        COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}/src"
                -P "${CMAKE_SOURCE_DIR}/tests/check_no_page_queries.cmake"
    )

    set(PBPFPS_UAL_X86 "${CMAKE_SOURCE_DIR}/research/tools/ultimate-asi-loader/dinput8.dll")
    if(EXISTS "${PBPFPS_UAL_X86}")
        add_executable(asi_loader_smoke_host tests/asi_loader_smoke_host.cpp)
        target_compile_features(asi_loader_smoke_host PRIVATE cxx_std_20)
        target_compile_definitions(asi_loader_smoke_host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_compile_options(asi_loader_smoke_host PRIVATE /W4 /WX /permissive- /utf-8)
        target_link_libraries(asi_loader_smoke_host PRIVATE winmm)
        set_property(TARGET asi_loader_smoke_host PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )

        set(PBPFPS_LOADER_SMOKE_DIR "${CMAKE_BINARY_DIR}/loader-smoke")
        add_custom_target(prepare_loader_smoke ALL
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${PBPFPS_LOADER_SMOKE_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:asi_loader_smoke_host>"
                    "${PBPFPS_LOADER_SMOKE_DIR}/asi_loader_smoke_host.exe"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:PenumbraBlackPlagueFpsFix>"
                    "${PBPFPS_LOADER_SMOKE_DIR}/PenumbraBlackPlague.FPSFix.asi"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/config/PenumbraBlackPlague.FPSFix.ini"
                    "${PBPFPS_LOADER_SMOKE_DIR}/PenumbraBlackPlague.FPSFix.ini"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${PBPFPS_UAL_X86}"
                    "${PBPFPS_LOADER_SMOKE_DIR}/winmm.dll"
            DEPENDS asi_loader_smoke_host PenumbraBlackPlagueFpsFix
            VERBATIM
        )
        add_test(NAME asi_loader_winmm_smoke
            COMMAND "${CMAKE_COMMAND}"
                    "-DHOST=${PBPFPS_LOADER_SMOKE_DIR}/asi_loader_smoke_host.exe"
                    "-DSTAGE=${PBPFPS_LOADER_SMOKE_DIR}"
                    -P "${CMAKE_SOURCE_DIR}/tests/run_loader_smoke.cmake"
        )
    endif()
endif()
