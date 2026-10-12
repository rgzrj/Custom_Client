# Public FFmpeg headers for the pinned Qt runtime

Public HEADERS from libavcodec, libavutil and libswscale in official FFmpeg 7.1.5.
Original copyrights are retained; see COPYING.LGPLv2.1 and LICENSE.md.
SOURCE.json records the source URL/archive SHA256, header hashes, observed DLL
hashes and configure line. Only avconfig.h is generated locally for x86_64
little-endian / fast unaligned access. No runtime DLL or encoder is vendored.

The Windows MinGW linker reads exports from Qt's shared LGPL DLLs directly.
Decoder initialization checks avcodec 61.19.101, avutil 59.39.100 and swscale
8.3.100 against the public headers. The supported runtime is the pinned Qt
6.11.2 mingw_64 installation; other platforms/versions require a separate SDK
and ABI validation. Public headers are not Qt private implementation headers.

Packaging verifies the observed DLL hashes, includes these license/provenance
notices and the checksum-verified 7.1.5 source archive plus configure line.
The external Huge Mas/开源/tmp archive is a local copy for inspection; building
does not depend on that path. A new Release package/clean-machine check remains
part of release acceptance.
