# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

cmake_minimum_required(VERSION 3.20)
project(VoicesOfTheVoidHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
endif()

# MinHook (for the GetPlayerViewPoint trampoline) comes from the copy vendored
# in cameraunlock-core, which its hook manager picks up below when no `minhook`
# target has been defined. Fetching it here instead put a github.com clone on
# every clean configure - including the release job, which packages after the
# tag has already been pushed - and compiled the result straight into a DLL
# loaded in the player's process. Core's copy is v1.3.4 and needs no network;
# its one local change is recorded in its own LOCAL-CHANGES.md and in
# THIRD-PARTY-NOTICES.md.

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/tracking.cpp
    src/udp_link.cpp
    src/view_hook.cpp
    src/hook_log.cpp
    src/mod_hotkeys.cpp
    src/window_centering.cpp
    src/reticle.cpp
    src/process_event_hook.cpp
    src/player_rig.cpp
    src/ue_call.cpp
    src/camera_boundary.cpp
    src/camera_fov.cpp
    src/aim_trace.cpp
    src/kismet_trace.cpp
    src/game_state.cpp
    src/lean_trace.cpp
    src/footage_view.cpp
    src/flashlight.cpp
    src/dev_console.cpp
    src/dev/dev_commands.cpp
    src/dev/dev_probes.cpp
    src/dev/dev_ue.cpp
    src/ue_reflect.cpp
    src/ue_vm.cpp
    src/config.cpp
    src/crash_report.cpp
    src/builds/build_registry.cpp
    src/builds/itch_offsets.cpp
)

# The development command channel. Off by default, and never on in a shipped
# build: its verbs dispatch arbitrary UFUNCTIONs, run console commands, open
# levels and move the pawn, which is a cheat tool however it is labelled, and
# it reaches game classes this repository has no business shipping a handle to
# in a player's build. Without this definition the channel's
# translation units compile to nothing but the empty entry points the mod calls,
# so the .asi a player installs cannot be talked into any of it, whatever
# HeadTracking.ini says. A local test session configures with
# -DVOTV_DEV_COMMANDS=ON.
option(VOTV_DEV_COMMANDS "Build the development command channel (local testing only)" OFF)

add_library(VoicesOfTheVoidHeadTracking SHARED ${MOD_SOURCES})

if(VOTV_DEV_COMMANDS)
    target_compile_definitions(VoicesOfTheVoidHeadTracking PRIVATE VOTV_DEV_COMMANDS)
endif()

target_compile_definitions(VoicesOfTheVoidHeadTracking PRIVATE
    VOTV_HT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(VoicesOfTheVoidHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(VoicesOfTheVoidHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# A linker map alongside the .asi. A fault offset in a Windows Error Reporting
# entry is the only forensic this mod gets from a player's machine - there is no
# symbol server and no debugger on the far end - and without a map it names
# nothing. Costs a text file next to the binary and is not shipped.
target_link_options(VoicesOfTheVoidHeadTracking PRIVATE /MAP)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(VoicesOfTheVoidHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Behaviour locks for the pure-function parts: the caller gate, the axis
# conversion, the projection, the trace frames, the config reader, the dev
# command channel's number parsing and the UDP link walk. Separate executables
# that link none of the hook or Windows machinery, so `pixi run test` runs with
# no game installed.
option(VOTV_BUILD_TESTS "Build the behaviour tests" ON)
if(VOTV_BUILD_TESTS)
    enable_testing()

    # The UDP link: how the receiver's flags read from outside, and the walk a
    # port held by another game takes on its way to receiving. Pure - no socket,
    # so it runs the same on a machine with the port already busy.
    add_executable(votv_udp_link_tests
        tests/udp_link_tests.cpp
        src/udp_link.cpp
    )
    target_include_directories(votv_udp_link_tests PRIVATE
        src
        tests
    )
    add_test(NAME votv_udp_link_tests COMMAND votv_udp_link_tests)

    # The other half of the same scenario, against a real socket: a held port,
    # the wait, and how long after it comes free the receiver binds it and
    # delivers a pose. Holds the port with a socket of its own on an OS-assigned
    # port number, so it needs no second process and never fights whatever is on
    # 4242 on the machine running it.
    add_executable(votv_udp_recovery_tests
        tests/udp_recovery_tests.cpp
    )
    target_include_directories(votv_udp_recovery_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(votv_udp_recovery_tests PRIVATE cameraunlock ws2_32)
    add_test(NAME votv_udp_recovery_tests COMMAND votv_udp_recovery_tests)

    # The session fed finite but absurd numbers from the tracker port, and the
    # guard that keeps its non-finite output out of the engine's view.
    add_executable(votv_pose_overflow_tests
        tests/pose_overflow_tests.cpp
    )
    target_include_directories(votv_pose_overflow_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(votv_pose_overflow_tests PRIVATE cameraunlock ws2_32)
    add_test(NAME votv_pose_overflow_tests COMMAND votv_pose_overflow_tests)

    # The bounds guard every reflection-derived parameter frame is written
    # behind. Header-only, so it links nothing: the point is that it can be run
    # anywhere, including on a machine with no game and no graphics device.
    add_executable(votv_ue_reflect_bounds_tests
        tests/ue_reflect_bounds_tests.cpp
    )
    target_include_directories(votv_ue_reflect_bounds_tests PRIVATE
        src
        tests
    )
    add_test(NAME votv_ue_reflect_bounds_tests COMMAND votv_ue_reflect_bounds_tests)

    # Where the mark goes, and what the field of view does to it. Pure: the
    # tangent maths and the aim direction are header-only, so this runs with no
    # game, no window and no graphics device.
    add_executable(votv_projection_tests
        tests/projection_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(votv_projection_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME votv_projection_tests COMMAND votv_projection_tests)

    # What the zoom compensation does to the pose and what it leaves alone.
    # Pure: the shaping is header-only, so it runs with no game.
    add_executable(votv_pose_shaping_tests
        tests/pose_shaping_tests.cpp
    )
    target_include_directories(votv_pose_shaping_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME votv_pose_shaping_tests COMMAND votv_pose_shaping_tests)

    # The tracker-to-Unreal boundary and the caller gate, locked at the numbers
    # the UE siblings verified in game.
    add_executable(votv_boundary_tests
        tests/boundary_tests.cpp
        src/camera_boundary.cpp
    )
    target_include_directories(votv_boundary_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME votv_boundary_tests COMMAND votv_boundary_tests)

    # Whether the head pose reaches the view, and the order the reasons it does
    # not are asked in. Header-only, so it links nothing.
    add_executable(votv_tracking_gate_tests
        tests/tracking_gate_tests.cpp
    )
    target_include_directories(votv_tracking_gate_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    add_test(NAME votv_tracking_gate_tests COMMAND votv_tracking_gate_tests)

    # The parameter frame both traces are cast through: what the fill puts
    # where, given a layout resolved out of the engine's reflection data. Pure
    # over a byte buffer, so it runs with no game and no physics.
    add_executable(votv_kismet_trace_tests
        tests/kismet_trace_tests.cpp
        src/kismet_trace.cpp
        src/ue_reflect.cpp
        src/builds/build_registry.cpp
        src/builds/itch_offsets.cpp
    )
    target_include_directories(votv_kismet_trace_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(votv_kismet_trace_tests PRIVATE cameraunlock cameraunlock_unreal)
    add_test(NAME votv_kismet_trace_tests COMMAND votv_kismet_trace_tests)

    # What HeadTracking.ini is allowed to put into the camera. Writes real INI
    # files into a temp directory and reads them back through the same
    # GetPrivateProfile* path the game uses, because that reader's parsing rules
    # are the thing being pinned.
    add_executable(votv_config_tests
        tests/config_tests.cpp
        src/config.cpp
    )
    target_include_directories(votv_config_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(votv_config_tests PRIVATE cameraunlock)
    add_test(NAME votv_config_tests COMMAND votv_config_tests)

    # The walk from an engine-supplied object index to its table slot, against a
    # synthetic object table in the test process's own memory - so the bounds
    # and the zero-divisor cases run with no game.
    add_executable(votv_object_table_tests
        tests/object_table_tests.cpp
        src/ue_call.cpp
        src/ue_reflect.cpp
        src/ue_vm.cpp
        src/builds/build_registry.cpp
        src/builds/itch_offsets.cpp
    )
    target_include_directories(votv_object_table_tests PRIVATE
        src
        tests
        cameraunlock-core/cpp/include
    )
    target_link_libraries(votv_object_table_tests PRIVATE cameraunlock cameraunlock_unreal)
    add_test(NAME votv_object_table_tests COMMAND votv_object_table_tests)

    # What a number typed into the dev command channel is allowed to look
    # like. Header-only, so it links nothing.
    add_executable(votv_dev_parse_tests
        tests/dev_parse_tests.cpp
    )
    target_include_directories(votv_dev_parse_tests PRIVATE
        src
        tests
    )
    add_test(NAME votv_dev_parse_tests COMMAND votv_dev_parse_tests)

    # Whether a player's game window gets moved, and where to. Header-only
    # geometry over rects, so it runs with no game and no window of its own -
    # which is the point: the cases it covers are monitor and taskbar layouts
    # this machine does not have.
    add_executable(votv_window_placement_tests
        tests/window_placement_tests.cpp
    )
    target_include_directories(votv_window_placement_tests PRIVATE
        src
        tests
    )
    add_test(NAME votv_window_placement_tests COMMAND votv_window_placement_tests)

    # What a parameter frame shows the script VM before the mod writes into it.
    # Only ue_call.h's Frame is exercised, which is header-only, so this links
    # nothing and needs no game.
    add_executable(votv_ue_call_frame_tests
        tests/ue_call_frame_tests.cpp
    )
    target_include_directories(votv_ue_call_frame_tests PRIVATE
        src
        tests
    )
    add_test(NAME votv_ue_call_frame_tests COMMAND votv_ue_call_frame_tests)
endif()
