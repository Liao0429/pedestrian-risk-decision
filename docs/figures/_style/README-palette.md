# 论文插图配色规范（Bsafiusion / UniFuse）

`docs/figures/_style/` 下的统一调色板：全部图表从 `palette.py` 取色，不再手写 hex。
只定义颜色与 rcParams，**不读取、不修改任何实验结果数据**（`results/` 保持原样）。

| 文件 | 作用 |
| --- | --- |
| `palette.py` | 两套主题的颜色常量 + `apply(theme)` + 顺序/发散/分类色图 |
| `palette_preview_bsafiusion.png` | Bsafiusion 三面板自检图（三级风险柱 / 多系列线 / 发散热力图） |
| `palette_preview_unifuse.png` | UniFuse 同上 |
| `README-palette.md` | 本文：取色依据、hex 清单、使用场景 |

预览图由 `python docs/figures/_style/palette.py` 生成（用的是合成示意数据，不含实验数字）。

---

## 1. 取色依据

两套主题不是随手取色，而是把两篇论文插图的视觉语言转译成可复用调色板：

### Bsafiusion（AAAI 2025）

> *Bsafiusion: A bidirectional stepwise feature alignment network for unaligned medical image fusion*, AAAI 2025.

论文插图的特征是**高饱和、强对比**：不同模态特征与网络模块用互不相似的高纯度色相一次性区分开，
视觉冲击强，读者一眼能记住"哪个色块对应哪个模块"。

转译规则：
- 主色相横跨蓝 / 青 / 绿 / 黄 / 橙 / 红 / 洋红 / 紫 —— 色相角拉开 ≥30°，保证相邻系列不糊在一起；
- 明度同时拉开（L* 42–86），即使被色盲模拟压扁色相，仍靠深浅分得开；
- 用于方法总览图、消融对比、双轴图这类"需要强调差异"的场合。

### UniFuse（ICCV 2025）

> *UniFuse: A Unified All-in-One Framework for Multi-Modal Medical Image Fusion Under Diverse Degradations and Misalignments*, ICCV 2025.

论文插图的特征是**冷调、沉稳、蓝灰深浅分层**：整套图基本在同一蓝色家族里，用明度阶梯表现
多模态融合的层级与流程，专业克制、简洁大气。

转译规则：
- 主序列是同族蓝色的明度阶梯（L* 16→86），外加一个青灰（`#3F7A8C`）作第二色家族，
  用来区分"另一路模态"，不破坏整体的冷调；
- 暖色只保留两个：WARNING 琥珀与 accent 珊瑚，且只出现在语义位置（风险等级 / 最优值标注），
  不做大面积填充；
- 用于框架示意图、热力图、趋势图这类"需要秩序感"的场合。

> 说明：仓库内没有这两篇论文的 PDF 原图，故本调色板是**按论文插图的风格特征重建**（色相分布、
> 明度层次、冷暖倾向），不是对 PDF 的像素级取色。若后续拿到原图要按像素对齐，只需替换
> `palette.py` 里 `primary` / `seq_stops` / `div_stops` 的 hex，其余代码不用动。

---

## 2. Bsafiusion 主题（`apply("bsafiusion")`）

### 2.1 分类主色序列（9 色，也用作 `axes.prop_cycle`）

| # | hex | L* | 用途 |
| --- | --- | --- | --- |
| 1 | `#0057FF` | 44.3 | 电光蓝 —— 主干 / 编码器、主折线（默认第一系列） |
| 2 | `#00C2FF` | 73.4 | 亮青 —— 解码器 / 融合支路、第二系列 |
| 3 | `#00C389` | 70.1 | 翡翠绿 —— 特征对齐模块（**必须配 marker/纹理**，见 §4） |
| 4 | `#FFD400` | 86.2 | 亮黄 —— 注意力权重、柱状图 |
| 5 | `#FF8A00` | 69.1 | 橙 —— 融合输出、双轴图第二系列 |
| 6 | `#E4003A` | 48.0 | 品红红 —— DANGER、误差、损失曲线 |
| 7 | `#B5179E` | 42.6 | 洋红 —— 对比方法（baseline） |
| 8 | `#7B2FF7` | 42.7 | 紫 —— 消融变体 |
| 9 | `#64748B` | 48.3 | 蓝灰 —— 参考线 / 中性基线 |

### 2.2 强调色与语义色

| 角色 | hex | L* | 用途 |
| --- | --- | --- | --- |
| accent（强调） | `#FF2E88` | 57.1 | 最优值描边、箭头、重点标注框。**只做标注，不做数据系列** |
| SAFE | `#00B884` | 66.5 | 三级风险最低档（柱/扇区填充 + 无纹理） |
| WARNING | `#FFB000` | 77.5 | 三级风险中间档（配 `//` 斜纹） |
| DANGER | `#E4003A` | 48.0 | 三级风险最高档（配 `xx` 交叉纹） |

### 2.3 背景 / 网格 / 文字

| 角色 | hex | 用途 |
| --- | --- | --- |
| bg | `#FFFFFF` | 画布底色（印刷用纯白） |
| panel | `#F5F8FF` | 子图/表头/图例底衬、分区带 |
| grid | `#D9E2F0` | 网格线（0.6pt，置于数据之下） |
| text | `#0B1F3A` | 标题、轴标签、正文数字 |
| text_muted | `#5A6B85` | 刻度标签、注释、次要说明 |
| axis | `#0B1F3A` | 轴线与边框（仅左/下） |

### 2.4 色图

- **顺序色图** `cmap_seq("bsafiusion")`（`bsafiusion_seq`）：
  `#F4FAFF → #B7E4FF → #4FB8FF → #0A6BFF → #0033CC → #001A4D`
  用途：密度热力图、注意力权重、单向量纲（浅=低，深=高）。
- **发散色图** `cmap_div("bsafiusion")`（`bsafiusion_div`）：
  `#001A4D → #0057FF → #8FC2FF → #F6F7F9 → #FFC46B → #FF8A00 → #8A2E00`
  用途：基线−增强差值、提升量、相关系数（负=蓝，0=近白，正=橙）。蓝↔橙是最稳的色盲安全发散配。
- **分类色图** `cmap_cat("bsafiusion")`（`bsafiusion_cat`）：上面 9 色构成的 `ListedColormap`，
  用于 `scatter(c=类别)` / 分类栅格，离散不插值。

---

## 3. UniFuse 主题（`apply("unifuse")`）

### 3.1 分类主色序列（9 色）

| # | hex | L* | 用途 |
| --- | --- | --- | --- |
| 1 | `#0F2A44` | 16.4 | 墨蓝 —— 主干 / 最深一档、标题 |
| 2 | `#1B3F63` | 25.8 | 深藏蓝 —— 编码器 |
| 3 | `#2E6E9B` | 44.4 | 中蓝 —— 解码器 |
| 4 | `#4A8DB7` | 56.0 | 亮蓝 —— 融合支路 |
| 5 | `#3F7A8C` | 48.1 | 青灰 —— 第二色家族（区分另一路模态） |
| 6 | `#7FA9C4` | 67.2 | 雾蓝 —— 辅助系列 |
| 7 | `#A9C6DB` | 78.4 | 浅雾蓝 —— 背景带 / 次要系列 |
| 8 | `#CBD8E3` | 85.7 | 浅蓝灰 —— 最浅一档 / 分隔带 |
| 9 | `#64748B` | 48.3 | 石板灰 —— 中性 / 基线 |

前 8 色即"蓝色家族明度阶梯"，做层级块（框架图各层）时按 1→8 递增使用最稳。

### 3.2 强调色与语义色

| 角色 | hex | L* | 用途 |
| --- | --- | --- | --- |
| accent（强调） | `#E07A5F` | 62.2 | 最优值标注、重点框（全套唯一暖色，克制使用） |
| SAFE | `#3F7A8C` | 48.1 | 三级风险最低档 |
| WARNING | `#D9A441` | 70.7 | 三级风险中间档（配 `//` 斜纹） |
| DANGER | `#8C2F39` | 33.8 | 三级风险最高档（配 `xx` 交叉纹） |

### 3.3 背景 / 网格 / 文字

| 角色 | hex | 用途 |
| --- | --- | --- |
| bg | `#FFFFFF` | 画布底色 |
| panel | `#F4F7FA` | 子图底衬、分区带 |
| grid | `#DCE4EC` | 网格线 |
| text | `#10243A` | 标题、轴标签、正文数字 |
| text_muted | `#5B6B7C` | 刻度标签、注释 |
| axis | `#10243A` | 轴线与边框 |

### 3.4 色图

- **顺序色图** `cmap_seq("unifuse")`（`unifuse_seq`）：
  `#F7FAFC → #D6E5EF → #A9C6DB → #6E9DBE → #2E6E9B → #1B3F63 → #0F2A44`
- **发散色图** `cmap_div("unifuse")`（`unifuse_div`）：
  `#0F2A44 → #2E6E9B → #9EC1DA → #F2F5F8 → #E3C7AE → #B0795A → #5E3B26`
  （冷蓝 ↔ 暖灰褐，低饱和，用作差值图不会抢主图）
- **分类色图** `cmap_cat("unifuse")`（`unifuse_cat`）：9 色 `ListedColormap`。

---

## 4. 色盲友好

两套主题都遵守同一条硬规则：**颜色永远不是唯一区分手段**。

1. **三级风险**除色相外必须叠加纹理：`HATCH = {"safe": "", "warning": "//", "danger": "xx"}`。
   SAFE/WARNING/DANGER 的 L\* 分别为 66.5/77.5/48.0（Bsafiusion）与 48.1/70.7/33.8（UniFuse），
   相邻两档明度差 ≥9、首尾差 ≥18，灰度打印也能读。
2. **多系列折线**用 `MARKERS` 轮换 marker（`o s ^ D v P X * h`），不只靠颜色。
3. 分类图里若同时出现绿与橙（Bsafiusion 第 3、5 位），务必配 marker 或直接标注 —— 红绿色弱下这两色会趋同。
4. 同一张图建议 ≤5 个分类系列；超过就改用小倍数图（subplots 分面），别把 9 色塞进一张图。
5. 发散色图统一蓝↔橙 / 冷蓝↔暖褐，不用红↔绿。

---

## 5. 用法

```python
import sys; sys.path.insert(0, "docs/figures/_style")
from palette import apply, colors, level_colors, cmap_seq, cmap_div, HATCH, MARKERS

t = apply("bsafiusion")          # 或 apply("unifuse")；dpi=300、去上右边框、中文回退已设好

# 1) 三级风险堆叠柱：色 + 纹理双通道
import matplotlib.pyplot as plt
lv = level_colors()
for i, (name, hexv) in enumerate(lv.items()):
    ax.bar(x + i*w, y[name], color=hexv, hatch=HATCH[name],
           edgecolor=t.axis, linewidth=0.5, label=name.upper())

# 2) 多系列折线：色 + marker
for i, (label, ys) in enumerate(series.items()):
    ax.plot(xs, ys, color=colors()[i], marker=MARKERS[i], label=label)

# 3) 热力图
im = ax.imshow(density, cmap=cmap_seq())      # 密度/权重
im = ax.imshow(delta,  cmap=cmap_div())       # 差值/提升量
fig.colorbar(im, ax=ax, fraction=0.046, pad=0.03)

fig.savefig("docs/figures/xxx.png")           # dpi=300 已由 apply() 设好
```

`apply()` 关键 rcParams：`figure.dpi=savefig.dpi=300`、`axes.spines.top/right=False`、
`axes.grid=True`（网格色取主题 `grid`）、`font.family` 为 `DejaVu Sans → SimHei → Microsoft YaHei → Noto Sans CJK SC` 的可用字体回退链（缺字逐字回退，中文不会变方块）、
`axes.unicode_minus=False`（负号正常）、`axes.prop_cycle` 取主题主色序列、`image.cmap` 取主题顺序色图。

`font_scale=0.9` 可整体缩小字号（双栏排版）；`dpi=` 可临时改分辨率。

---

## 6. 约束

- 本目录只提供颜色，不改 `results/`、不改任何实验数值与统计口径；
- 换主题只影响观感，不影响数据：同一份数据在两套主题下画出的数值必须完全一致；
- 新增图表请引用 `palette.py`，不要再在脚本里硬写 hex，避免出现第三套配色。