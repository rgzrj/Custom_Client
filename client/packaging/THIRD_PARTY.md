# Bundled runtime dependencies

This application dynamically links to Qt 6 and uses Qt Multimedia's bundled
FFmpeg backend and directly calls FFmpeg for upstream HEVC reception. Runtime DLLs may be replaced with compatible builds; they are
not statically incorporated into the application's executable.

- Qt: Copyright The Qt Company Ltd. and other contributors. License alternatives,
  third-party copyrights, versions and source locations are recorded in the
  accompanying `licenses/*.spdx.json` and Qt license files. The deployed Qt runtime
  modules used here are available under LGPL terms.
  Source: https://download.qt.io/archive/qt/6.11/6.11.2/submodules/
- FFmpeg: Copyright the FFmpeg developers, LGPL 2.1 or later with accompanying
  permissive third-party licenses. This preview's installed runtime reports
  version 7.1.5; `manifest.json` records the version from the deployed DLL.
  Qt's reference SBOM and attribution page still declare 7.1.3; preserve them
  as upstream reference notices, not as proof of the deployed binary version.
  See `licenses/FFmpeg.html` for the full license and copyright texts.
  Corresponding upstream source: https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz
  License information: https://ffmpeg.org/legal.html
  Native HEVC uses the matching public headers; SOURCE.json records observed
  runtime hashes/configuration. Packaging checks those hashes and includes the
  verified source archive and LGPL notice. No FFmpeg code is statically linked.
- MinGW-w64 and GCC runtime libraries are supplied by the matching Qt MinGW
  toolchain. GCC runtime redistribution uses the GCC Runtime Library Exception.
  MinGW-w64 runtime source and notices: https://www.mingw-w64.org/
  GCC runtime exception: https://www.gnu.org/licenses/gcc-exception-3.1.html

The files in `licenses/` include Qt's bundled third-party copyright notices and
software bills of materials. They do not assign a license to this project's
own code or to user-provided artwork.

The normal executable also statically incorporates the pinned RM26_Client data
backend and its source-built dependencies:

- RM26_Client e291f2f: MIT, Copyright (c) 2025–2026 Fudan University EGA Team.
  See `licenses/RM26_Client-LICENSE` and `RM26_Client-PATCHES.md` for local changes.
  Source: https://github.com/ClearWei/RM26_Client/tree/e291f2f65f5a5924509e4e2fe1b148ff9d4b24af
- Protocol Buffers 29.3: BSD 3-Clause. Source: https://github.com/protocolbuffers/protobuf/tree/v29.3
- Abseil 20240116.2: Apache 2.0. Source: https://github.com/abseil/abseil-cpp/tree/20240116.2
- utf8_range, bundled with Protobuf: MIT. Its separate license is included.
- Eclipse Paho MQTT C 1.3.14: distributed here under its Eclipse Distribution
  License 1.0 (BSD 3-Clause alternative). The full upstream notices and both
  license alternatives are included. The build's bounded packet factory preserves
  upstream copyright and is described in the project source.
  Source: https://github.com/eclipse-paho/paho.mqtt.c/tree/v1.3.14

All of these license texts accompany the package in `licenses/`. Protoc and
Python are development tools and are not runtime requirements.
