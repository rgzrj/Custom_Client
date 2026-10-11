cmake_minimum_required(VERSION 3.21)

project(RM27CustomTerminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rm27_core
    src/core/terminal_state.cpp
    src/core/connection_profile.cpp
    src/core/freshness_grade.cpp
    src/core/input_mapping.cpp
    src/core/pairing_session.cpp
    src/core/video_source_policy.cpp
)
target_include_directories(rm27_core PUBLIC src)

include(CTest)
option(RM27_BUILD_RM2026_PROBE "Build the isolated loopback-only RM2026 C++ probe" OFF)
option(RM27_RM2026_LOCAL_HUD "Build the real QML terminal as a loopback-only historical test HUD" OFF)
option(RM27_RM2026_VIDEO_HUD "Enable the isolated read-only RM2026 HEVC video receiver" OFF)
if(RM27_RM2026_VIDEO_HUD AND RM27_RM2026_LOCAL_HUD)
    message(FATAL_ERROR "Use separate builds for the video experiment and MQTT historical HUD")
endif()
if(RM27_RM2026_LOCAL_HUD AND NOT RM27_BUILD_RM2026_PROBE)
    message(FATAL_ERROR "RM27_RM2026_LOCAL_HUD requires RM27_BUILD_RM2026_PROBE")
endif()
if(RM27_BUILD_RM2026_PROBE)
    add_subdirectory(contracts/rm2026-v2.0.0/native)
endif()
if(BUILD_TESTING)
    if(WIN32)
        find_program(RM27_POWERSHELL_EXECUTABLE NAMES powershell.exe REQUIRED)
        add_test(NAME rm27_video_launcher_tests
            COMMAND "${RM27_POWERSHELL_EXECUTABLE}" -NoProfile -File
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_video_launcher.ps1")
        set_tests_properties(rm27_video_launcher_tests PROPERTIES TIMEOUT 15)
    endif()
    # Historical RM2026 tests; the normal terminal does not link this reference.
    add_executable(rm2026_video_tests tests/test_rm2026_video.cpp)
    target_link_libraries(rm2026_video_tests PRIVATE rm2026_video_reference)
    add_test(NAME rm2026_video_tests COMMAND rm2026_video_tests)
    add_executable(rm2026_hevc_sync_tests tests/test_hevc_sync_filter.cpp)
    target_link_libraries(rm2026_hevc_sync_tests PRIVATE rm2026_video_reference)
    add_test(NAME rm2026_hevc_sync_tests COMMAND rm2026_hevc_sync_tests)

    add_executable(rm27_core_tests tests/test_terminal_state.cpp)
    target_link_libraries(rm27_core_tests PRIVATE rm27_core)
    add_test(NAME rm27_core_tests COMMAND rm27_core_tests)

    add_executable(rm27_connection_profile_tests tests/test_connection_profile.cpp)
    target_link_libraries(rm27_connection_profile_tests PRIVATE rm27_core)
    add_test(NAME rm27_connection_profile_tests COMMAND rm27_connection_profile_tests)

    add_executable(rm27_core_policies_tests tests/test_core_policies.cpp)
    target_link_libraries(rm27_core_policies_tests PRIVATE rm27_core)
    add_test(NAME rm27_core_policies_tests COMMAND rm27_core_policies_tests)
    add_executable(rm27_pairing_tests tests/test_pairing_session.cpp)
    target_link_libraries(rm27_pairing_tests PRIVATE rm27_core)
    add_test(NAME rm27_pairing_tests COMMAND rm27_pairing_tests)
endif()

if(BUILD_TESTING OR RM27_RM2026_VIDEO_HUD OR RM27_RM2026_LOCAL_HUD)
    add_library(rm2026_video_reference src/protocol/rm2026/video_reassembly.cpp
        src/video/hevc_sync_filter.cpp)
    target_include_directories(rm2026_video_reference PUBLIC src)
endif()

option(RM27_BUILD_APP "Build the Qt 6 operator terminal" ON)
if(RM27_BUILD_APP)
    find_package(Qt6 6.9 COMPONENTS Network Quick QuickControls2 Multimedia REQUIRED)
    if(Qt6_FOUND)
        qt_standard_project_setup(REQUIRES 6.9)

        add_library(rm27_runtime_log src/app/runtime_log.cpp)
        target_include_directories(rm27_runtime_log PUBLIC src)
        target_link_libraries(rm27_runtime_log PUBLIC Qt6::Core)

        if(RM27_RM2026_VIDEO_HUD OR RM27_RM2026_LOCAL_HUD)
            add_library(rm2026_video_receiver src/video/historical_video_receiver.cpp
                src/video/historical_video_receiver.hpp)
            target_link_libraries(rm2026_video_receiver PUBLIC rm2026_video_reference Qt6::Network)
        endif()

        # —— 内置机器人线稿（素材由用户提供，按原文件名一一对应）——
        # 原文件名 → 资源别名：别名用 ASCII，避开工具链与文件系统编码差异；
        # 中文原名保留在本列表与 qml/RobotPhotos.qml 的对应表里，改名即可换图。
        # 实际内置的是 pitcure/hud/ 下的 HUD 版（透明底 + 白线，由
        # `python tools/prepare_robot_photos.py` 从原图确定性生成；原图是白底黑线，
        # 在深色 HUD 上会糊成亮块）。缺少 HUD 版时回退原图并告警。
        # 飞镖.jpg 本次未内置（等正式协议确认飞镖状态再接）。
        set(RM27_ROBOT_PHOTO_FILES
            "pitcure/哨兵.png"
            "pitcure/无人机.jpg"
            "pitcure/步兵3.png"
            "pitcure/步兵4.png"
            "pitcure/工程.jpg"
            "pitcure/英雄.png"
        )
        set(RM27_ROBOT_PHOTO_ALIASES
            sentry.png aerial.jpg infantry3.png infantry4.png engineer.jpg hero.png
        )

        # 应用与窗口测试都要能解码 :/rm27/robots/*，因此两个目标都要挂同一份资源。
        function(rm27_add_robot_photos target)
            list(LENGTH RM27_ROBOT_PHOTO_FILES photoCount)
            math(EXPR photoLast "${photoCount} - 1")
            set(photoPaths "")
            foreach(photoIndex RANGE ${photoLast})
                list(GET RM27_ROBOT_PHOTO_FILES ${photoIndex} photoFile)
                list(GET RM27_ROBOT_PHOTO_ALIASES ${photoIndex} photoAlias)
                get_filename_component(photoStem "${photoFile}" NAME_WE)
                set(hudPhoto "pitcure/hud/${photoStem}.png")
                if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${hudPhoto}")
                    set(photoPath "${CMAKE_CURRENT_SOURCE_DIR}/${hudPhoto}")
                else()
                    message(WARNING
                        "缺少 HUD 线稿 ${hudPhoto}：请先运行 python tools/prepare_robot_photos.py；本次内置原图")
                    set(photoPath "${CMAKE_CURRENT_SOURCE_DIR}/${photoFile}")
                endif()
                set_source_files_properties("${photoPath}" PROPERTIES QT_RESOURCE_ALIAS "${photoAlias}")
                list(APPEND photoPaths "${photoPath}")
            endforeach()
            qt_add_resources(${target} "rm27_robot_photos"
                PREFIX "/rm27/robots"
                FILES ${photoPaths}
            )
        endfunction()

        qt_add_executable(rm27_terminal
            src/app/main.cpp
            src/app/terminal_controller.cpp
            src/app/terminal_controller.hpp
            src/app/connection_settings.cpp
            src/app/connection_settings.hpp
            src/sim/sim_referee_receiver.cpp
            src/sim/sim_referee_receiver.hpp
            src/sim/sim_mqtt_receiver.cpp
            src/sim/sim_mqtt_receiver.hpp
            src/video/udp_video_ingress.cpp
            src/video/udp_video_ingress.hpp
        )

        qt_add_qml_module(rm27_terminal
            URI RM27.Terminal
            VERSION 1.0
            QML_FILES qml/Main.qml qml/Theme.qml qml/HudOverlay.qml qml/OperatorCamera.qml
                      qml/StartPanel.qml qml/RobotPhotos.qml qml/DisplaySettings.qml qml/BattlefieldMap.qml
        )

        # Release 使用 Windows GUI 子系统，双击时不额外弹出控制台。
        set_target_properties(rm27_terminal PROPERTIES WIN32_EXECUTABLE "$<CONFIG:Release>")

        rm27_add_robot_photos(rm27_terminal)

        target_link_libraries(rm27_terminal PRIVATE
            rm27_core
            rm27_runtime_log
            Qt6::Network
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Multimedia
        )
        if(RM27_RM2026_LOCAL_HUD)
            target_compile_definitions(rm27_terminal PRIVATE RM27_RM2026_LOCAL_HUD=1)
            target_link_libraries(rm27_terminal PRIVATE rm2026_mqtt_reference rm2026_video_receiver)
        endif()
        if(RM27_RM2026_VIDEO_HUD)
            target_compile_definitions(rm27_terminal PRIVATE RM27_RM2026_VIDEO_HUD=1)
            target_link_libraries(rm27_terminal PRIVATE rm2026_video_receiver)
        endif()

        if(BUILD_TESTING)
            find_package(Qt6 REQUIRED COMPONENTS Test)
            if(RM27_RM2026_VIDEO_HUD)
                add_executable(rm2026_video_receiver_tests tests/test_historical_video_receiver.cpp)
                target_link_libraries(rm2026_video_receiver_tests PRIVATE rm2026_video_receiver
                    Qt6::Test Qt6::Multimedia Qt6::Quick)
                add_test(NAME rm2026_video_receiver_tests COMMAND rm2026_video_receiver_tests)
                set_tests_properties(rm2026_video_receiver_tests PROPERTIES
                    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40)
            endif()
            add_executable(rm27_runtime_log_tests tests/test_runtime_log.cpp)
            target_link_libraries(rm27_runtime_log_tests PRIVATE rm27_runtime_log Qt6::Test)
            add_test(NAME rm27_runtime_log_tests COMMAND rm27_runtime_log_tests)
            add_executable(rm27_playback_tests
                tests/test_video_playback.cpp
                src/app/terminal_controller.cpp
                src/app/terminal_controller.hpp
                src/app/connection_settings.cpp
                src/app/connection_settings.hpp
                src/sim/sim_referee_receiver.cpp
                src/sim/sim_referee_receiver.hpp
                src/sim/sim_mqtt_receiver.cpp
                src/sim/sim_mqtt_receiver.hpp
                src/video/udp_video_ingress.cpp
                src/video/udp_video_ingress.hpp
            )
            target_link_libraries(rm27_playback_tests PRIVATE rm27_core rm27_runtime_log Qt6::Test
                Qt6::Network Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
            add_test(NAME rm27_diagnostics_tests COMMAND rm27_playback_tests
                unsupportedFormalProfile guiHeartbeatLateTick videoDiagnosticSources)
            add_test(NAME rm27_pairing_workflow_tests COMMAND rm27_playback_tests pairingWorkflow)
            set_tests_properties(rm27_pairing_workflow_tests PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
            set_tests_properties(rm27_diagnostics_tests PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
            set(RM27_TEST_HEVC "" CACHE FILEPATH "Generated Annex-B HEVC fixture for the isolated experiment")
            if(RM27_RM2026_VIDEO_HUD)
                target_link_libraries(rm27_playback_tests PRIVATE rm2026_video_receiver)
                target_compile_definitions(rm27_playback_tests PRIVATE RM27_RM2026_VIDEO_HUD=1)
                if(EXISTS "${RM27_TEST_HEVC}")
                    add_test(NAME rm2026_hevc_hud_tests COMMAND rm27_playback_tests historicalHevcUdp)
                    set_tests_properties(rm2026_hevc_hud_tests PROPERTIES
                        ENVIRONMENT "RM27_TEST_HEVC=${RM27_TEST_HEVC}" TIMEOUT 40)
                endif()
            endif()
            rm27_add_robot_photos(rm27_playback_tests)
            target_compile_definitions(rm27_playback_tests PRIVATE
                RM27_QML_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
            set(RM27_TEST_VIDEO "" CACHE FILEPATH "Local video fixture for playback integration tests")
            set(RM27_TEST_TS "" CACHE FILEPATH "Optional generated MPEG-TS fixture for UDP decode tests")
            if(EXISTS "${RM27_TEST_TS}")
                add_test(NAME rm27_udp_video_tests COMMAND rm27_playback_tests simulationUdpVideo)
                set_tests_properties(rm27_udp_video_tests PROPERTIES
                    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RM27_TEST_TS=${RM27_TEST_TS}" TIMEOUT 45)
            endif()
            add_test(NAME rm27_historical_hud_tests COMMAND rm27_playback_tests
                historicalHudPresentation historicalGamePresentation)
            add_test(NAME rm27_ui_layout_tests COMMAND rm27_playback_tests uiHierarchy)
            set_tests_properties(rm27_ui_layout_tests PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=${CMAKE_CURRENT_BINARY_DIR}/unavailable-test-fonts"
                TIMEOUT 15)
            set_tests_properties(rm27_historical_hud_tests PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
            if(RM27_RM2026_LOCAL_HUD)
                find_package(Python3 COMPONENTS Interpreter QUIET)
                if(Python3_Interpreter_FOUND)
                    add_test(NAME rm2026_hud_demo_tests COMMAND "${Python3_EXECUTABLE}"
                        "${PROJECT_SOURCE_DIR}/tests/test_rm2026_demo.py" --exe $<TARGET_FILE:rm27_terminal>)
                    set_tests_properties(rm2026_hud_demo_tests PROPERTIES
                        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 50)
                    if(EXISTS "${RM27_TEST_HEVC}")
                        add_test(NAME rm2026_hud_hevc_demo_tests COMMAND "${Python3_EXECUTABLE}"
                            "${PROJECT_SOURCE_DIR}/tests/test_rm2026_demo.py" --exe $<TARGET_FILE:rm27_terminal>
                            --hevc-file "${RM27_TEST_HEVC}" DemoChecks.test_hevc_scenario)
                        set_tests_properties(rm2026_hud_hevc_demo_tests PROPERTIES
                            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 50)
                    endif()
                    if(EXISTS "${RM27_TEST_TS}")
                        add_test(NAME rm2026_hud_video_tests COMMAND "${Python3_EXECUTABLE}"
                            "${PROJECT_SOURCE_DIR}/tests/test_rm2026_demo.py" --exe $<TARGET_FILE:rm27_terminal>
                            --video-ts "${RM27_TEST_TS}" DemoChecks.test_video_scenario)
                        set_tests_properties(rm2026_hud_video_tests PROPERTIES
                            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 50)
                    endif()
                endif()
            endif()
            if(EXISTS "${RM27_TEST_VIDEO}")
                add_test(NAME rm27_playback_tests COMMAND rm27_playback_tests)
                set_tests_properties(rm27_playback_tests PROPERTIES
                    ENVIRONMENT "RM27_TEST_VIDEO=${RM27_TEST_VIDEO};RM27_TEST_HEVC=${RM27_TEST_HEVC}"
                    TIMEOUT 60)
            endif()
            add_executable(rm27_sim_protocol_tests
                tests/test_sim_referee_protocol.cpp
                src/sim/sim_referee_receiver.cpp
                src/sim/sim_referee_receiver.hpp
            )
            target_include_directories(rm27_sim_protocol_tests PRIVATE src)
            target_compile_definitions(rm27_sim_protocol_tests PRIVATE
                RM27_CONTRACT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/contracts/sim-v1-fixtures.json")
            target_link_libraries(rm27_sim_protocol_tests PRIVATE rm27_core Qt6::Network)
            add_test(NAME rm27_sim_protocol_tests COMMAND rm27_sim_protocol_tests)
            add_executable(rm27_sim_mqtt_tests
                tests/test_sim_mqtt.cpp
                src/sim/sim_mqtt_receiver.cpp
                src/sim/sim_mqtt_receiver.hpp
                src/sim/sim_referee_receiver.cpp
                src/sim/sim_referee_receiver.hpp
            )
            target_include_directories(rm27_sim_mqtt_tests PRIVATE src)
            target_link_libraries(rm27_sim_mqtt_tests PRIVATE rm27_core Qt6::Network Qt6::Test)
            add_test(NAME rm27_sim_mqtt_tests COMMAND rm27_sim_mqtt_tests)
        endif()
    else()
        message(WARNING "Qt 6.9+ was not found; building the protocol-independent core only.")
    endif()
endif()

# assert-based checks must run in Release packages too, not become empty tests.
foreach(testTarget rm27_core_tests rm27_connection_profile_tests rm27_core_policies_tests rm27_pairing_tests rm27_sim_protocol_tests rm2026_video_tests rm2026_hevc_sync_tests)
    if(TARGET ${testTarget})
        target_compile_options(${testTarget} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>
            $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
    endif()
endforeach()
