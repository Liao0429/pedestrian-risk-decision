# 图表配色规范

本仓库图表统一使用一套**学术沉稳**配色（`palette.py` 中的 `steady` 主题）：低饱和冷调蓝灰单色阶，
靠明度阶梯区分层级，风险三级用明度差而非色相撞色，久看不刺眼，适合论文插图与正式汇报。

## 主序列（9 色）

| # | 色值 | 用途 |
|---|---|---|
| 1 | `#16324F` | 墨蓝 — 主干 / 最深一档 / 标题 |
| 2 | `#24506F` | 深藏蓝 — 编码器 |
| 3 | `#356F94` | 中蓝 — 解码器 |
| 4 | `#5B93B5` | 亮蓝 — 融合支路 |
| 5 | `#4E7F84` | 青灰 — 第二色家族（区分另一模态） |
| 6 | `#8FAEC4` | 雾蓝 — 辅助系列 |
| 7 | `#B6CBD9` | 浅雾蓝 — 背景带 / 次要系列 |
| 8 | `#D3DEE7` | 浅蓝灰 — 网格带 / 最浅一档 |
| 9 | `#6B7A89` | 石板灰 — 中性 / 基线 |

- 强调色：`#3E6B9B`（沉稳靛蓝，用于最优值标注与重点框，不使用暖色撞色）
- 面板 / 网格：`#F3F6F9` / `#DDE5EC`
- 正文 / 次级文字：`#12293D` / `#5E6E7E`

## 风险三级

| 等级 | 色值 | 明度 L* | 纹理 | marker |
|---|---|---|---|---|
| SAFE | `#3D7C8F` | ≈49 | 无 | `o` |
| WARNING | `#B08127` | ≈58 | `//` | `s` |
| DANGER | `#8C2F39` | ≈33 | `xx` | `^` |

三级不靠颜色单一通道区分：同时叠加纹理（hatch）与 marker 形状，灰度打印与色盲读者均可分辨。

## 用法

```python
import sys; sys.path.insert(0, "docs/figures/_style")
from palette import apply, colors, level_colors, cmap_seq, cmap_div

apply("steady")                        # 设好 rcParams（字体 / dpi / 去上右框 / 配色循环）
plt.bar(x, y, color=level_colors()["danger"])
plt.imshow(mat, cmap=cmap_seq())       # 顺序色图（密度 / 权重）
plt.imshow(delta, cmap=cmap_div())     # 发散色图（差值 / 提升量）
```

`palette.py` 只定义颜色与 rcParams，不读、不写任何实验结果数据。

## 重制全部图表

```bash
G:/DevTools/python/python.exe docs/figures/_style/make_figures.py              # 全部数据图 + 首屏主视觉
G:/DevTools/python/python.exe docs/figures/_style/make_figures.py --hero-only  # 只重出 hero_overview.png
```

`make_figures.py` 不重写绘图逻辑：它把 `scripts/plot_*.py` 里的硬编码色值映射到本主题，
再执行原脚本，因此数据口径与原始脚本完全一致。
