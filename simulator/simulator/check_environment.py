"""只检查 Python/Tk 和本机端口，不连接服务或修改网卡。"""

import socket
import sys
try:
    import tkinter as tk
except ImportError as error:
    raise SystemExit("缺少 Tkinter，请安装包含 Tcl/Tk 的 Python 运行环境") from error


def main():
    if sys.version_info < (3, 10):
        raise SystemExit("需要 Python 3.10 或更高版本")
    try:
        root = tk.Tk()
    except tk.TclError as error:
        raise SystemExit(f"Tk 无法创建窗口，请检查 Tcl/Tk 安装与图形环境：{error}") from error
    root.withdraw()
    print(f"Python {sys.version.split()[0]} / Tk {root.tk.call('info', 'patchlevel')} 检查通过")
    root.destroy()
    for port, protocol in ((39001, socket.SOCK_DGRAM), (39002, socket.SOCK_DGRAM),
                           (39003, socket.SOCK_DGRAM), (39004, socket.SOCK_STREAM)):
        with socket.socket(socket.AF_INET, protocol) as probe:
            if sys.platform == "win32":
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            try:
                probe.bind(("127.0.0.1", port))
            except OSError:
                print(f"提醒：端口 {port} 已占用，请确认是否已有客户端/模拟器；不会关闭它。")
    print("只检查了接收端口；无法检测所有持续发包会话。未修改系统网络。")


if __name__ == "__main__":
    main()
