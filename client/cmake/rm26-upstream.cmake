set(upstreamRoot "${PROJECT_SOURCE_DIR}/third_party/rm26_client/src")
if(RM27_HAS_UPSTREAM)
    set(upstreamCpp
        network/MqttManager.cpp network/NetworkManager.cpp network/Protocol.cpp network/CRC.cpp
        core/GameData.cpp core/TimedEventRules.cpp core/PopupStateMachine.cpp
        config/ConfigManager.cpp config/ConfigLoader.cpp config/ConfigValidator.cpp)
    list(TRANSFORM upstreamCpp PREPEND "${upstreamRoot}/")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/robomaster.pb.cc" "${CMAKE_CURRENT_BINARY_DIR}/robomaster.pb.h"
        COMMAND "${RM2026_PROTOC}" "--cpp_out=${CMAKE_CURRENT_BINARY_DIR}"
            "--proto_path=${upstreamRoot}/network/proto" "${upstreamRoot}/network/proto/robomaster.proto"
        DEPENDS "${upstreamRoot}/network/proto/robomaster.proto" VERBATIM)
    add_library(rm26_upstream STATIC ${upstreamCpp}
        "${CMAKE_CURRENT_BINARY_DIR}/robomaster.pb.cc"
        "${PROJECT_SOURCE_DIR}/src/protocol/rm2026/upstream_reports.cpp"
        "${PROJECT_SOURCE_DIR}/src/protocol/rm2026/upstream_backend.cpp"
        "${PROJECT_SOURCE_DIR}/src/protocol/rm2026/upstream_commands.cpp"
        "${PROJECT_SOURCE_DIR}/src/protocol/rm2026/state_mapping.cpp")
    set_target_properties(rm26_upstream PROPERTIES AUTOMOC ON)
    target_compile_definitions(rm26_upstream PUBLIC RM27_UPSTREAM_BACKEND=1 PAHO_MQTT_STATIC=1 PRIVATE RM_HAS_MQTT=1)
    target_include_directories(rm26_upstream PUBLIC "${PROJECT_SOURCE_DIR}/src"
        PRIVATE "${upstreamRoot}" "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(rm26_upstream PUBLIC rm27_core Qt6::Core Qt6::Network Qt6::Gui
        PRIVATE protobuf::libprotobuf eclipse-paho-mqtt-c::paho-mqtt3c-static)

endif()

add_library(rm26_hevc STATIC
    "${upstreamRoot}/network/HevcDecoder.cpp" "${upstreamRoot}/network/HevcDecoder.h"
    "${PROJECT_SOURCE_DIR}/src/video/upstream_video.cpp")
set_target_properties(rm26_hevc PROPERTIES AUTOMOC ON)
target_compile_definitions(rm26_hevc PUBLIC RM27_UPSTREAM_VIDEO=1 PRIVATE RM_ENABLE_FFMPEG=1)
target_include_directories(rm26_hevc PUBLIC "${PROJECT_SOURCE_DIR}/src"
    PRIVATE "${upstreamRoot}" "${PROJECT_SOURCE_DIR}/third_party/ffmpeg-7.1.5/include")
target_link_libraries(rm26_hevc PUBLIC Qt6::Core Qt6::Network Qt6::Gui PRIVATE rm2026_video_reference)

# Match the public headers to the LGPL shared runtime supplied by the pinned Qt.
# MinGW ld reads the DLL export table directly; no MSVC/static FFmpeg SDK needed.
if(NOT WIN32 OR NOT MINGW)
    message(FATAL_ERROR "Upstream HEVC currently requires the pinned Windows MinGW Qt runtime")
endif()
get_target_property(qtCoreRuntime Qt6::Core IMPORTED_LOCATION_RELEASE)
get_filename_component(qtRuntimeDirectory "${qtCoreRuntime}" DIRECTORY)
foreach(ffmpegDll avcodec-61 avutil-59 swscale-8)
    if(NOT EXISTS "${qtRuntimeDirectory}/${ffmpegDll}.dll")
        message(FATAL_ERROR "Missing matching Qt FFmpeg runtime: ${ffmpegDll}.dll")
    endif()
    target_link_libraries(rm26_hevc PRIVATE "${qtRuntimeDirectory}/${ffmpegDll}.dll")
endforeach()
if(RM27_HAS_UPSTREAM)
    target_link_libraries(rm26_upstream PUBLIC rm26_hevc)
endif()
