# Bundled runtime dependencies

This application dynamically links to Qt 6 and uses Qt Multimedia's bundled
FFmpeg backend. Runtime DLLs may be replaced with compatible builds; they are
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
- MinGW-w64 and GCC runtime libraries are supplied by the matching Qt MinGW
  toolchain. GCC runtime redistribution uses the GCC Runtime Library Exception.
  MinGW-w64 runtime source and notices: https://www.mingw-w64.org/
  GCC runtime exception: https://www.gnu.org/licenses/gcc-exception-3.1.html

The files in `licenses/` include Qt's bundled third-party copyright notices and
software bills of materials. They do not assign a license to this project's
own code or to user-provided artwork.
