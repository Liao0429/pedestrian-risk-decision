#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""两套论文插图配色规范（matplotlib 即用）。

主题
----
bsafiusion  AAAI 2025《Bsafiusion: A bidirectional stepwise feature alignment
            network for unaligned medical image fusion》—— 高饱和、强对比，
            用于区分不同模态 / 模块，视觉冲击强。
unifuse     ICCV 2025《UniFuse: A Unified All-in-One Framework for Multi-Modal
            Medical Image Fusion Under Diverse Degradations and Misalignments》
            —— 冷调蓝灰深浅层次，沉稳高级，适合统一框架类示意图。

用法
----
    from palette import apply, colors, level_colors, cmap_seq, cmap_div
    apply("unifuse")                 # 设好 rcParams（字体/dpi/去上右框/配色循环）
    plt.bar(x, y, color=level_colors()["danger"])
    plt.imshow(mat, cmap=cmap_seq())   # 顺序色图（密度 / 权重）
    plt.imshow(delta, cmap=cmap_div()) # 发散色图（差值 / 提升量）

色盲友好
--------
两套主题都不把红绿当唯一区分手段：同一张图里三级风险/多条曲线除色相外，
还必须叠加 hatch（见 HATCH）与 marker（见 MARKERS）或直接标注。各主题内
SAFE/WARNING/DANGER 的明度差 >= 15（L*），灰度打印仍可分辨。

本模块只定义颜色与 rcParams，不读、不写任何实验结果数据。
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from functools import lru_cache

import matplotlib
import matplotlib.pyplot as plt
from cycler import cycler
from matplotlib.colors import LinearSegmentedColormap, ListedColormap

__all__ = [
    "Theme",
    "THEMES",
    "LEVELS",
    "HATCH",
    "MARKERS",
    "get",
    "colors",
    "level_colors",
    "cmap_cat",
    "cmap_seq",
    "cmap_div",
    "apply",
]

_HEX_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")

#: 三级风险键（全项目统一口径，顺序即 SAFE→WARNING→DANGER）
LEVELS = ("safe", "warning", "danger")

#: 三级风险的填充纹理；与颜色配合，保证黑白打印/色盲下仍可区分
HATCH = {"safe": "", "warning": "//", "danger": "xx"}

#: 分类系列的 marker 轮换（线图用，避免只靠颜色区分）
MARKERS = ("o", "s", "^", "D", "v", "P", "X", "*", "h")


def _font_chain(*names: str) -> list[str]:
    """按本机实际安装的字体过滤出回退链（DejaVu Sans 打底，中文用 SimHei/雅黑/Noto）。"""
    from matplotlib import font_manager

    installed = {f.name for f in font_manager.fontManager.ttflist}
    chain = [n for n in names if n in installed]
    return chain or ["sans-serif"]


#: 字体回退链（缺字逐字回退，避免中文变方块）
_FONTS = _font_chain(
    "DejaVu Sans", "SimHei", "Microsoft YaHei", "Noto Sans CJK SC", "Arial Unicode MS"
)


@dataclass(frozen=True)
class Theme:
    """一套主题的全部颜色槽位；所有值均为 6 位 hex。"""

    key: str
    label: str
    source: str
    intent: str
    # 分类主色序列（>=8），按“最重要 -> 次要”排序，也用作 axes.prop_cycle
    primary: tuple[str, ...]
    accent: str          # 强调色：仅用于标注/最优值描边/箭头，不做数据系列
    safe: str
    warning: str
    danger: str
    bg: str              # 画布底色
    panel: str           # 子图/表头底色
    grid: str
    text: str
    text_muted: str
    axis: str            # 轴线/边框
    seq_stops: tuple[str, ...]   # 顺序色图（浅 -> 深）
    div_stops: tuple[str, ...]   # 发散色图（负 -> 0 -> 正）

    def __post_init__(self) -> None:
        if len(self.primary) < 8:
            raise ValueError(f"{self.key}: 主色序列需 >=8 色，当前 {len(self.primary)}")
        if len(self.seq_stops) < 3 or len(self.div_stops) < 3:
            raise ValueError(f"{self.key}: 顺序/发散色图至少各需 3 个停靠点")
        for name, value in self._slots().items():
            if not _HEX_RE.match(value):
                raise ValueError(f"{self.key}.{name} 不是合法 hex: {value!r}")
        for name in ("seq_stops", "div_stops", "primary"):
            for value in getattr(self, name):
                if not _HEX_RE.match(value):
                    raise ValueError(f"{self.key}.{name} 含非法 hex: {value!r}")

    def _slots(self) -> dict[str, str]:
        return {
            "accent": self.accent, "safe": self.safe, "warning": self.warning,
            "danger": self.danger, "bg": self.bg, "panel": self.panel,
            "grid": self.grid, "text": self.text, "text_muted": self.text_muted,
            "axis": self.axis,
        }

    @property
    def cmap_seq_name(self) -> str:
        return f"{self.key}_seq"

    @property
    def cmap_div_name(self) -> str:
        return f"{self.key}_div"

    @property
    def cmap_cat_name(self) -> str:
        return f"{self.key}_cat"


BSAFIUSION = Theme(
    key="bsafiusion",
    label="Bsafiusion（AAAI 2025，高饱和强对比）",
    source="Bsafiusion: A bidirectional stepwise feature alignment network "
           "for unaligned medical image fusion, AAAI 2025",
    intent="高饱和、强对比；用互不相似的高纯度色相把不同模态/模块一次分清，"
           "适合方法总览图、消融对比、双轴图。",
    primary=(
        "#0057FF",  # 电光蓝  主干/编码器、主折线
        "#00C2FF",  # 亮青    解码器/融合支路
        "#00C389",  # 翡翠绿  特征对齐模块（需配 marker）
        "#FFD400",  # 亮黄    注意力权重/柱状
        "#FF8A00",  # 橙      融合输出、双轴第二系列
        "#E4003A",  # 品红红  DANGER/误差/损失曲线
        "#B5179E",  # 洋红    对比方法（baseline）
        "#7B2FF7",  # 紫      消融变体
        "#64748B",  # 蓝灰    参考线/中性基线
    ),
    accent="#FF2E88",     # 霓虹粉：最优值描边、箭头、重点标注（非数据系列）
    safe="#00B884",       # 翡翠绿：SAFE（L*≈66）
    warning="#FFB000",    # 琥珀：WARNING（L*≈76）
    danger="#E4003A",     # 品红红：DANGER（L*≈47）
    bg="#FFFFFF",
    panel="#F5F8FF",
    grid="#D9E2F0",
    text="#0B1F3A",
    text_muted="#5A6B85",
    axis="#0B1F3A",
    seq_stops=("#F4FAFF", "#B7E4FF", "#4FB8FF", "#0A6BFF", "#0033CC", "#001A4D"),
    div_stops=("#001A4D", "#0057FF", "#8FC2FF", "#F6F7F9", "#FFC46B", "#FF8A00", "#8A2E00"),
)

UNIFUSE = Theme(
    key="unifuse",
    label="UniFuse（ICCV 2025，冷调蓝灰层次）",
    source="UniFuse: A Unified All-in-One Framework for Multi-Modal Medical "
           "Image Fusion Under Diverse Degradations and Misalignments, ICCV 2025",
    intent="冷调、单色系深浅分层；用同一蓝色家族的明度阶梯表现统一框架的层级与"
           "多模态融合流程，专业克制，适合框架图、热力图、趋势图。",
    primary=(
        "#0F2A44",  # 墨蓝    主干/最深一档、标题
        "#1B3F63",  # 深藏蓝  编码器
        "#2E6E9B",  # 中蓝    解码器
        "#4A8DB7",  # 亮蓝    融合支路
        "#3F7A8C",  # 青灰    第二色家族（区分另一模态）
        "#7FA9C4",  # 雾蓝    辅助系列
        "#A9C6DB",  # 浅雾蓝  背景带/次要系列
        "#CBD8E3",  # 浅蓝灰  网格带/最浅一档
        "#64748B",  # 石板灰  中性/基线
    ),
    accent="#E07A5F",     # 低饱和珊瑚：最优值标注、重点框（唯一暖色，克制使用）
    safe="#3F7A8C",       # 青灰蓝：SAFE（L*≈50）
    warning="#D9A441",    # 琥珀：WARNING（L*≈68）
    danger="#8C2F39",     # 酒红：DANGER（L*≈33）
    bg="#FFFFFF",
    panel="#F4F7FA",
    grid="#DCE4EC",
    text="#10243A",
    text_muted="#5B6B7C",
    axis="#10243A",
    seq_stops=("#F7FAFC", "#D6E5EF", "#A9C6DB", "#6E9DBE", "#2E6E9B", "#1B3F63", "#0F2A44"),
    div_stops=("#0F2A44", "#2E6E9B", "#9EC1DA", "#F2F5F8", "#E3C7AE", "#B0795A", "#5E3B26"),
)

THEMES = {t.key: t for t in (BSAFIUSION, UNIFUSE)}


def get(theme: str | Theme = "bsafiusion") -> Theme:
    """按名字取主题对象；已传对象则原样返回。"""
    if isinstance(theme, Theme):
        return theme
    try:
        return THEMES[theme]
    except KeyError:
        raise ValueError(f"未知主题 {theme!r}，可选：{', '.join(THEMES)}") from None


def colors(theme: str | Theme = "bsafiusion") -> list[str]:
    """分类主色序列（可直接给 color= 或 zip 到多系列）。"""
    return list(get(theme).primary)


def level_colors(theme: str | Theme = "bsafiusion") -> dict[str, str]:
    """SAFE/WARNING/DANGER -> hex；配套 HATCH 使用。"""
    t = get(theme)
    return {k: getattr(t, k) for k in LEVELS}


@lru_cache(maxsize=None)
def _cmap(name: str) -> matplotlib.colors.Colormap:
    return matplotlib.colormaps[name]


def _register(theme: Theme) -> None:
    """把两套色图注册进 matplotlib（幂等，重复 import 不报错）。"""
    for name, stops in (
        (theme.cmap_seq_name, theme.seq_stops),
        (theme.cmap_div_name, theme.div_stops),
    ):
        if name in matplotlib.colormaps:
            continue
        matplotlib.colormaps.register(
            LinearSegmentedColormap.from_list(name, list(stops), N=256), name=name
        )
    if theme.cmap_cat_name not in matplotlib.colormaps:
        matplotlib.colormaps.register(
            ListedColormap(list(theme.primary), name=theme.cmap_cat_name),
            name=theme.cmap_cat_name,
        )


for _t in THEMES.values():
    _register(_t)


def cmap_cat(theme: str | Theme = "bsafiusion") -> ListedColormap:
    """分类色图（柱/散点按类别着色，离散、不插值）。"""
    return _cmap(get(theme).cmap_cat_name)  # type: ignore[return-value]


def cmap_seq(theme: str | Theme = "bsafiusion") -> LinearSegmentedColormap:
    """顺序色图：密度、注意力权重、单向量纲（浅 -> 深）。"""
    return _cmap(get(theme).cmap_seq_name)  # type: ignore[return-value]


def cmap_div(theme: str | Theme = "bsafiusion") -> LinearSegmentedColormap:
    """发散色图：基线-增强差值、提升量、相关系数（负 -> 0 -> 正）。"""
    return _cmap(get(theme).cmap_div_name)  # type: ignore[return-value]


def apply(theme: str | Theme = "bsafiusion", *, dpi: int = 300, font_scale: float = 1.0) -> Theme:
    """应用主题到全局 rcParams 并返回主题对象。

    dpi         : 画布与保存分辨率（论文默认 300）
    font_scale  : 整体字号缩放（双栏排版可给 0.9）
    """
    t = get(theme)
    plt.rcParams.update({
        # 画布 / 输出
        "figure.dpi": dpi,
        "savefig.dpi": dpi,
        "savefig.bbox": "tight",
        "savefig.facecolor": t.bg,
        "figure.facecolor": t.bg,
        "axes.facecolor": t.bg,
        "savefig.pad_inches": 0.05,
        # 字体：DejaVu Sans 优先，中文回退到 SimHei / 雅黑 / Noto
        # 字体族显式列成链：DejaVu Sans 优先，缺字逐字回退到 SimHei/雅黑/Noto
        "font.family": _FONTS,
        "font.sans-serif": _FONTS,
        "font.size": 11 * font_scale,
        "axes.titlesize": 13 * font_scale,
        "axes.labelsize": 11 * font_scale,
        "xtick.labelsize": 9.5 * font_scale,
        "ytick.labelsize": 9.5 * font_scale,
        "legend.fontsize": 9.5 * font_scale,
        "axes.unicode_minus": False,
        # 轴线 / 网格
        "axes.edgecolor": t.axis,
        "axes.linewidth": 0.8,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.grid": True,
        "grid.color": t.grid,
        "grid.linewidth": 0.6,
        "grid.alpha": 1.0,
        "axes.axisbelow": True,
        # 文字
        "text.color": t.text,
        "axes.labelcolor": t.text,
        "axes.titlecolor": t.text,
        "xtick.color": t.text_muted,
        "ytick.color": t.text_muted,
        # 线 / 图例 / 循环
        "lines.linewidth": 1.8,
        "lines.markersize": 5,
        "lines.markeredgewidth": 0.8,
        "legend.frameon": False,
        "axes.prop_cycle": cycler(color=list(t.primary)),
        # 热力图默认用本主题顺序色图
        "image.cmap": t.cmap_seq_name,
    })
    return t


def _demo(theme: str = "bsafiusion", out_dir: str = ".") -> str:
    """渲染一张三面板预览图（柱/线/热力图），用于目检配色，不涉及实验数据。"""
    import os

    import numpy as np

    t = apply(theme)
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.4))
    x = np.arange(5)

    ax = axes[0]
    for i, lv in enumerate(LEVELS):
        ax.bar(x + (i - 1) * 0.26, np.linspace(2, 5, 5) * (i + 1), width=0.25,
               color=getattr(t, lv), edgecolor=t.axis, linewidth=0.5,
               hatch=HATCH[lv], label=lv.upper())
    ax.set_title("三级风险（色+纹理）")
    ax.set_xticks(x, [f"V{i+1}" for i in x])
    ax.set_ylim(0, ax.get_ylim()[1] * 1.35)
    ax.legend(ncol=3, loc="upper left")

    ax = axes[1]
    for i, c in enumerate(t.primary[:5]):
        ax.plot(x, np.linspace(1, 4, 5) * (i + 1) * 0.6 + i * 0.3,
                color=c, marker=MARKERS[i], label=f"S{i+1}")
    ax.set_title("多系列（色+marker）")
    ax.legend(ncol=2, loc="upper left")

    ax = axes[2]
    grad = np.add.outer(np.linspace(-1, 1, 40), np.linspace(-1, 1, 40))
    im = ax.imshow(grad, cmap=cmap_div(t), aspect="auto")
    fig.colorbar(im, ax=ax, fraction=0.046, pad=0.03)
    ax.set_title("发散色图")
    ax.grid(False)
    ax.tick_params(length=0)

    fig.suptitle(f"{t.label}  ·  {t.key}", fontsize=12)
    fig.tight_layout()
    path = os.path.join(out_dir, f"palette_preview_{t.key}.png")
    fig.savefig(path)
    plt.close(fig)
    return path


if __name__ == "__main__":
    import os

    here = os.path.dirname(os.path.abspath(__file__))
    for key in THEMES:
        print("saved:", _demo(key, here))