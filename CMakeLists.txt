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
    src/core/video_source_policy.cpp
)
target_include_directories(rm27_core PUBLIC src)

include(CTest)
if(BUILD_TESTING)
    add_executable(rm27_core_tests tests/test_terminal_state.cpp)
    target_link_libraries(rm27_core_tests PRIVATE rm27_core)
    add_test(NAME rm27_core_tests COMMAND rm27_core_tests)

    add_executable(rm27_connection_profile_tests tests/test_connection_profile.cpp)
    target_link_libraries(rm27_connection_profile_tests PRIVATE rm27_core)
    add_test(NAME rm27_connection_profile_tests COMMAND rm27_connection_profile_tests)

    add_executable(rm27_core_policies_tests tests/test_core_policies.cpp)
    target_link_libraries(rm27_core_policies_tests PRIVATE rm27_core)
    add_test(NAME rm27_core_policies_tests COMMAND rm27_core_policies_tests)
endif()

option(RM27_BUILD_APP "Build the Qt 6 operator terminal" ON)
if(RM27_BUILD_APP)
    find_package(Qt6 6.9 COMPONENTS Network Quick QuickControls2 Multimedia REQUIRED)
    if(Qt6_FOUND)
        qt_standard_project_setup(REQUIRES 6.9)

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
                      qml/StartPanel.qml qml/RobotPhotos.qml
        )

        rm27_add_robot_photos(rm27_terminal)

        target_link_libraries(rm27_terminal PRIVATE
            rm27_core
            Qt6::Network
            Qt6::Quick
            Qt6::QuickControls2
            Qt6::Multimedia
        )

        if(BUILD_TESTING)
            find_package(Qt6 REQUIRED COMPONENTS Test)
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
            target_link_libraries(rm27_playback_tests PRIVATE rm27_core Qt6::Test
                Qt6::Network Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
            rm27_add_robot_photos(rm27_playback_tests)
            target_compile_definitions(rm27_playback_tests PRIVATE
                RM27_QML_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml")
            set(RM27_TEST_VIDEO "" CACHE FILEPATH "Local video fixture for playback integration tests")
            if(EXISTS "${RM27_TEST_VIDEO}")
                add_test(NAME rm27_playback_tests COMMAND rm27_playback_tests)
                set_tests_properties(rm27_playback_tests PROPERTIES
                    ENVIRONMENT "RM27_TEST_VIDEO=${RM27_TEST_VIDEO}"
                    TIMEOUT 60)
            endif()
            add_executable(rm27_sim_protocol_tests
                tests/test_sim_referee_protocol.cpp
                src/sim/sim_referee_receiver.cpp
                src/sim/sim_referee_receiver.hpp
            )
            target_include_directories(rm27_sim_protocol_tests PRIVATE src)
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
