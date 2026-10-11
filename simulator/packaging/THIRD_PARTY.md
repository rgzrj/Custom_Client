# Windows 模拟器运行库说明

程序由独立构建环境的 PyInstaller 6.22.3 生成，采用目录型便携包。Python/Tk 与下列 DLL 原样分发，无第三方客户端代码、素材或 FFmpeg CLI。必须保留整个包及 `licenses/`，不要只转发 exe。

当前打包基线：D:/YOLOV8 的 CPython 3.13.9（Anaconda）、Tk/Tcl 8.6.15；换运行时须重新核对实际 DLL、来源和许可，不沿用此清单。

| 运行组件 | 许可材料与来源 |
| --- | --- |
| CPython 3.13.9 | `licenses/Python.txt`，来自当前 Python 的 LICENSE_PYTHON.txt |
| Tcl/Tk 8.6.15 | Tk 的 `licenses/Tcl-Tk.txt` 与 Conda 包内 Tcl license.terms |
| PyInstaller bootloader | `licenses/PyInstaller.txt`，包含可分发生成程序的例外说明 |
| libffi 3.4.4、bzip2 1.0.8、zlib 1.3.1 | Conda 对应包 `info/licenses`，保留原版权正文 |
| OpenSSL libcrypto 3.0.18 | Apache-2.0，`licenses/OpenSSL.txt`；[官方该版本原文](https://github.com/openssl/openssl/blob/openssl-3.0.18/LICENSE.txt) |
| XZ liblzma 5.6.4 | 0BSD，仅分发库、不分发 xz CLI；[官方版本说明](https://github.com/tukaani-project/xz/blob/v5.6.4/COPYING)与 `licenses/XZ-0BSD.txt` |
| libmpdec 4.0.0 | BSD-2-Clause，`licenses/libmpdec.txt`；[上游](https://www.bytereef.org/mpdecimal/)；网络无法取得上游包时核对了 [4.0.0 原文镜像](https://github.com/Safeheron/mpdecimal/blob/v4.0.0/COPYRIGHT.txt)，仅保留许可，不复制实现 |
| Microsoft UCRT / VC Runtime | 对应 Conda 运行库包的 LICENSE 原文，位于 `licenses/ucrt-*`、`licenses/vc14_runtime-*` |

此文件是实际包的工程来源记录，不承诺所有外部许可义务已完成法律审查。升级任一运行库必须重新验证，不将打包工具依赖当成应用运行依赖。
