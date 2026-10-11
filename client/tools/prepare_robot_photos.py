"""把 pitcure/ 下的机器人线稿转成 HUD 用的「透明底 + 白线」资源。

为什么要转：原图是**白底黑线**的手绘/线稿图。深色 HUD 上直接铺白底会变成一块块
亮方块，缩到 49×30 时只剩噪点。转成透明底 + 白线后，和 HUD 自身的俯视线稿风格一致，
也保留了每台车可辨认的轮廓。

做法（确定性、可复现）：
1. 读原图（PNG / JPG 都行；注意有些文件扩展名与真实格式不一致，交给 Pillow 判断）；
2. 取灰度、按分位数拉伸对比度，压掉纸面灰底与浅色噪声；
3. alpha = 反相后的灰度（黑线不透明、白底透明），颜色统一为白色；
4. 输出 RGBA PNG 到 pitcure/hud/，文件名与原图一致。

用法：python tools/prepare_robot_photos.py
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image
import numpy as np

# 与 CMakeLists.txt 中内置的六个机型一一对应；飞镖本次不处理。
ROBOT_PHOTOS = [
    "哨兵.png",
    "无人机.jpg",
    "步兵3.png",
    "步兵4.png",
    "工程.jpg",
    "英雄.png",
]

# 线宽归一化：低于 FLOOR 的一律透明（压掉纸面灰与 JPEG 噪点），
# FLOOR..CEIL 之间线性过渡，CEIL 以上为实线。
INK_FLOOR = 0.25
INK_CEIL = 0.70

# 线稿最长边统一到这个尺寸，减小内置体积（HUD 里最大只用到 80 逻辑像素）。
MAX_EDGE = 512


def _ink_from_gray(values: np.ndarray, dark_background: bool) -> np.ndarray:
    """把灰度图变成「线为 1、底为 0」的墨迹强度。"""
    low = float(np.percentile(values, 1.0))
    high = float(np.percentile(values, 99.0))
    if high - low < 1e-3:  # 纯色图：没有可提取的线条，退化为全透明
        return np.zeros_like(values)
    # 白底黑线：越暗越像线；黑底白线：越亮越像线。
    normalized = (values - low) / (high - low) if dark_background else (high - values) / (high - low)
    normalized = np.clip(normalized, 0.0, 1.0)
    return np.clip((normalized - INK_FLOOR) / (INK_CEIL - INK_FLOOR), 0.0, 1.0)


def convert(source: Path, target: Path) -> tuple[int, int, float]:
    with Image.open(source) as raw:
        gray = raw.convert("L")
        if max(gray.size) > MAX_EDGE:
            scale = MAX_EDGE / max(gray.size)
            gray = gray.resize(
                (max(1, round(gray.width * scale)), max(1, round(gray.height * scale))),
                Image.LANCZOS,
            )
        values = np.asarray(gray, dtype=np.float32) / 255.0

    # 用边框一圈的均值判断底色极性：既支持白底黑线，也支持黑底白线。
    border = np.concatenate(
        [values[0, :], values[-1, :], values[:, 0], values[:, -1]]
    )
    dark_background = float(border.mean()) < 0.5
    ink = _ink_from_gray(values, dark_background)

    alpha = (ink * 255.0).astype(np.uint8)
    rgba = np.empty((*alpha.shape, 4), dtype=np.uint8)
    rgba[..., 0:3] = 255  # 白线
    rgba[..., 3] = alpha
    target.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgba, mode="RGBA").save(target, format="PNG", optimize=True)
    return alpha.shape[1], alpha.shape[0], float((alpha > 8).mean())


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    source_dir = root / "pitcure"
    target_dir = source_dir / "hud"
    if not source_dir.is_dir():
        print(f"找不到素材目录：{source_dir}", file=sys.stderr)
        return 1

    converted = 0
    for name in ROBOT_PHOTOS:
        source = source_dir / name
        if not source.exists():
            print(f"跳过缺失文件：{name}", file=sys.stderr)
            continue
        target = target_dir / f"{Path(name).stem}.png"
        width, height, ink_ratio = convert(source, target)
        converted += 1
        print(f"{name} → hud/{target.name}  {width}×{height}  线条像素占比 {ink_ratio:.3f}")
    print(f"完成：{converted} 张。HUD 资源目录：{target_dir}")
    return 0 if converted == len(ROBOT_PHOTOS) else 1


if __name__ == "__main__":
    raise SystemExit(main())
