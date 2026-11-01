# FACTS · 项目可引用真实事实表

> 自动生成，禁止手工估算。每行注明来源文件与提取方式；取不到的写「未采集」。
> 生成脚本：`tmp/_make_facts.py`（可重复运行）

共 **738** 条事实。

| # | 指标名 | 数值 | 来源文件 | 提取方式 |
|---|---|---|---|---|
| 1 | 图片评测 · 图片总数 | 500 | `results/images500_gt_eval/summary.json` | images_total |
| 2 | 图片评测 · GT 目标总数 | 3089 | `results/images500_gt_eval/summary.json` | gt_total |
| 3 | 图片评测 · 检出目标总数 | 2548 | `results/images500_gt_eval/summary.json` | det_total |
| 4 | 图片评测 · 整体检出率(%) | 82.49 | `results/images500_gt_eval/summary.json` | overall_detection_rate_pct |
| 5 | 图片评测 · 计数 MAE | 1.926 | `results/images500_gt_eval/summary.json` | mae |
| 6 | 图片评测 · 计数 RMSE | 3.188 | `results/images500_gt_eval/summary.json` | rmse |
| 7 | 图片评测 · Pearson r | 0.8022 | `results/images500_gt_eval/summary.json` | pearson_r |
| 8 | 图片评测 · 生成时间 | 2026-10-29 09:35:23 | `results/images500_gt_eval/summary.json` | generated_at |
| 9 | 图片评测 · 风险等级 SAFE 图片数 | 414 | `results/images500_gt_eval/summary.json` | level_counts |
| 10 | 图片评测 · 风险等级 WARNING 图片数 | 61 | `results/images500_gt_eval/summary.json` | level_counts |
| 11 | 图片评测 · 风险等级 DANGER 图片数 | 25 | `results/images500_gt_eval/summary.json` | level_counts |
| 12 | 图片评测 · GT 分桶 1-3 图片数 | 200 | `results/images500_gt_eval/summary.json` | bucket_stats[1-3].images |
| 13 | 图片评测 · GT 分桶 1-3 检出率(%) | 94.46 | `results/images500_gt_eval/summary.json` | bucket_stats[1-3].rate_pct |
| 14 | 图片评测 · GT 分桶 4-9 图片数 | 150 | `results/images500_gt_eval/summary.json` | bucket_stats[4-9].images |
| 15 | 图片评测 · GT 分桶 4-9 检出率(%) | 73.85 | `results/images500_gt_eval/summary.json` | bucket_stats[4-9].rate_pct |
| 16 | 图片评测 · GT 分桶 10-13 图片数 | 138 | `results/images500_gt_eval/summary.json` | bucket_stats[10-13].images |
| 17 | 图片评测 · GT 分桶 10-13 检出率(%) | 84.92 | `results/images500_gt_eval/summary.json` | bucket_stats[10-13].rate_pct |
| 18 | 图片评测 · GT 分桶 14-19 图片数 | 12 | `results/images500_gt_eval/summary.json` | bucket_stats[14-19].images |
| 19 | 图片评测 · GT 分桶 14-19 检出率(%) | 79.43 | `results/images500_gt_eval/summary.json` | bucket_stats[14-19].rate_pct |
| 20 | tinyGT · generated_at | 2026-10-29 10:38:56 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 21 | tinyGT · images_total | 500 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 22 | tinyGT · gt_total | 3089 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 23 | tinyGT · gt_consistency.ok | True | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 24 | tinyGT · gt_consistency.mismatches | [] | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 25 | tinyGT · gt_consistency.missing_labels | [] | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 26 | tinyGT · global_height.mean_px | 108.61 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 27 | tinyGT · global_height.median_px | 65.36003600000001 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 28 | tinyGT · tiny_height_counts.16 | 304 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 29 | tinyGT · tiny_height_counts.20 | 417 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 30 | tinyGT · tiny_height_counts.32 | 811 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 31 | tinyGT · tiny_height_counts.48 | 1224 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 32 | tinyGT · tiny_area_counts.1024 | 719 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 33 | tinyGT · tiny_area_counts.4096 | 1407 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 34 | tinyGT · correlation.tiny_frac_vs_rate | -0.4027 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 35 | tinyGT · correlation.median_h_vs_rate | 0.3065 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 36 | tinyGT · correlation.tiny_height_px | 20 | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 37 | tinyGT · tiny_bucket_mean_rate | list[5] | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 38 | tinyGT · case_details.worst_under_detected | [{"filename": "000000498796_13", "gt": 13, "det": 0, "err": -13, "tiny_lt20": 11, "tiny_lt32": 13, "median_height_px": 9.3, "min_height_px": 5.109984}, {"filename": "000000577592_13", "gt": 13, "det": | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 39 | tinyGT · case_details.worst_over_detected | [{"filename": "000000538291_13", "gt": 13, "det": 25, "err": 12, "tiny_lt20": 0, "tiny_lt32": 0, "median_height_px": 69.6, "min_height_px": 42.890106}, {"filename": "000000270071_13", "gt": 13, "det": | `results/images500_gt_eval/tiny_gt_summary.json` | tiny_gt_summary.json |
| 40 | 视频聚合[_aggregate.json] · total_videos | 111 | `results/rerun/_aggregate.json` | _aggregate.json |
| 41 | 视频聚合[_aggregate.json] · total_frames | 45021 | `results/rerun/_aggregate.json` | _aggregate.json |
| 42 | 视频聚合[_aggregate.json] · levels.SAFE | 28214 | `results/rerun/_aggregate.json` | _aggregate.json |
| 43 | 视频聚合[_aggregate.json] · levels.WARNING | 8355 | `results/rerun/_aggregate.json` | _aggregate.json |
| 44 | 视频聚合[_aggregate.json] · levels.DANGER | 8452 | `results/rerun/_aggregate.json` | _aggregate.json |
| 45 | 视频聚合[_aggregate.json] · switches.list | [4, 0, 12, 13, 2, 1, 11, 7, 6, 16, 0, 4, 6, 8, 6, 2, 26, 0, 4, 4, 1, 7, 8, 2, 2, 6, 8, 2, 3, 12, 0, 0, 23, 6, 0, 2, 0, 0, 4, 4, 6, 2, 4, 4, 14, 4, 13, 8, 12, 2, 8, 4, 2, 10, 8, 2, 10, 16, 14, 5, 3, 11 | `results/rerun/_aggregate.json` | _aggregate.json |
| 46 | 视频聚合[_aggregate.json] · switches.total | 568 | `results/rerun/_aggregate.json` | _aggregate.json |
| 47 | 视频聚合[_aggregate.json] · switches.mean | 5.1171 | `results/rerun/_aggregate.json` | _aggregate.json |
| 48 | 视频聚合[_aggregate.json] · switches.median | 4 | `results/rerun/_aggregate.json` | _aggregate.json |
| 49 | 视频聚合[_aggregate.json] · switches.min | 0 | `results/rerun/_aggregate.json` | _aggregate.json |
| 50 | 视频聚合[_aggregate.json] · switches.max | 26 | `results/rerun/_aggregate.json` | _aggregate.json |
| 51 | 视频聚合[_aggregate.json] · videos_with_danger | 45 | `results/rerun/_aggregate.json` | _aggregate.json |
| 52 | 视频聚合[_aggregate.json] · fiedler_by_level.SAFE | {"count": 28214, "mean": 0.0213, "median": 0, "std": 0.2033, "positive_count": 427, "positive_mean": 1.4053} | `results/rerun/_aggregate.json` | _aggregate.json |
| 53 | 视频聚合[_aggregate.json] · fiedler_by_level.WARNING | {"count": 8355, "mean": 0.567, "median": 0, "std": 0.875, "positive_count": 4027, "positive_mean": 1.1764} | `results/rerun/_aggregate.json` | _aggregate.json |
| 54 | 视频聚合[_aggregate.json] · fiedler_by_level.DANGER | {"count": 8452, "mean": 1.0842, "median": 0.716, "std": 1.1904, "positive_count": 6360, "positive_mean": 1.4409} | `results/rerun/_aggregate.json` | _aggregate.json |
| 55 | 视频聚合[_aggregate.json] · per_video.mixkit_13192 | {"frames": 305, "dist": {"SAFE": 6, "WARNING": 120, "DANGER": 179}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 56 | 视频聚合[_aggregate.json] · per_video.mixkit_13265 | {"frames": 281, "dist": {"SAFE": 281, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 57 | 视频聚合[_aggregate.json] · per_video.mixkit_134 | {"frames": 600, "dist": {"SAFE": 294, "WARNING": 69, "DANGER": 237}, "switches": 12} | `results/rerun/_aggregate.json` | _aggregate.json |
| 58 | 视频聚合[_aggregate.json] · per_video.mixkit_14084 | {"frames": 255, "dist": {"SAFE": 67, "WARNING": 159, "DANGER": 29}, "switches": 13} | `results/rerun/_aggregate.json` | _aggregate.json |
| 59 | 视频聚合[_aggregate.json] · per_video.mixkit_151 | {"frames": 336, "dist": {"SAFE": 326, "WARNING": 10, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 60 | 视频聚合[_aggregate.json] · per_video.mixkit_15914 | {"frames": 294, "dist": {"SAFE": 231, "WARNING": 63, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate.json` | _aggregate.json |
| 61 | 视频聚合[_aggregate.json] · per_video.mixkit_15997 | {"frames": 712, "dist": {"SAFE": 585, "WARNING": 127, "DANGER": 0}, "switches": 11} | `results/rerun/_aggregate.json` | _aggregate.json |
| 62 | 视频聚合[_aggregate.json] · per_video.mixkit_17630 | {"frames": 282, "dist": {"SAFE": 7, "WARNING": 170, "DANGER": 105}, "switches": 7} | `results/rerun/_aggregate.json` | _aggregate.json |
| 63 | 视频聚合[_aggregate.json] · per_video.mixkit_18264 | {"frames": 240, "dist": {"SAFE": 34, "WARNING": 33, "DANGER": 173}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 64 | 视频聚合[_aggregate.json] · per_video.mixkit_18272 | {"frames": 486, "dist": {"SAFE": 413, "WARNING": 73, "DANGER": 0}, "switches": 16} | `results/rerun/_aggregate.json` | _aggregate.json |
| 65 | 视频聚合[_aggregate.json] · per_video.mixkit_18305 | {"frames": 383, "dist": {"SAFE": 383, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 66 | 视频聚合[_aggregate.json] · per_video.mixkit_21326 | {"frames": 301, "dist": {"SAFE": 283, "WARNING": 18, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 67 | 视频聚合[_aggregate.json] · per_video.mixkit_221 | {"frames": 482, "dist": {"SAFE": 32, "WARNING": 93, "DANGER": 357}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 68 | 视频聚合[_aggregate.json] · per_video.mixkit_22427 | {"frames": 217, "dist": {"SAFE": 140, "WARNING": 45, "DANGER": 32}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 69 | 视频聚合[_aggregate.json] · per_video.mixkit_23168 | {"frames": 180, "dist": {"SAFE": 141, "WARNING": 39, "DANGER": 0}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 70 | 视频聚合[_aggregate.json] · per_video.mixkit_23327 | {"frames": 247, "dist": {"SAFE": 232, "WARNING": 15, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 71 | 视频聚合[_aggregate.json] · per_video.mixkit_23410 | {"frames": 703, "dist": {"SAFE": 486, "WARNING": 118, "DANGER": 99}, "switches": 26} | `results/rerun/_aggregate.json` | _aggregate.json |
| 72 | 视频聚合[_aggregate.json] · per_video.mixkit_23818 | {"frames": 217, "dist": {"SAFE": 217, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 73 | 视频聚合[_aggregate.json] · per_video.mixkit_241 | {"frames": 379, "dist": {"SAFE": 316, "WARNING": 63, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 74 | 视频聚合[_aggregate.json] · per_video.mixkit_25516 | {"frames": 222, "dist": {"SAFE": 180, "WARNING": 8, "DANGER": 34}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 75 | 视频聚合[_aggregate.json] · per_video.mixkit_25890 | {"frames": 309, "dist": {"SAFE": 4, "WARNING": 305, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate.json` | _aggregate.json |
| 76 | 视频聚合[_aggregate.json] · per_video.mixkit_28616 | {"frames": 152, "dist": {"SAFE": 5, "WARNING": 75, "DANGER": 72}, "switches": 7} | `results/rerun/_aggregate.json` | _aggregate.json |
| 77 | 视频聚合[_aggregate.json] · per_video.mixkit_31086 | {"frames": 202, "dist": {"SAFE": 154, "WARNING": 48, "DANGER": 0}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 78 | 视频聚合[_aggregate.json] · per_video.mixkit_32622 | {"frames": 281, "dist": {"SAFE": 273, "WARNING": 8, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 79 | 视频聚合[_aggregate.json] · per_video.mixkit_3428 | {"frames": 456, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 451}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 80 | 视频聚合[_aggregate.json] · per_video.mixkit_3429 | {"frames": 246, "dist": {"SAFE": 7, "WARNING": 35, "DANGER": 204}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 81 | 视频聚合[_aggregate.json] · per_video.mixkit_3469 | {"frames": 295, "dist": {"SAFE": 79, "WARNING": 216, "DANGER": 0}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 82 | 视频聚合[_aggregate.json] · per_video.mixkit_39767 | {"frames": 249, "dist": {"SAFE": 232, "WARNING": 17, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 83 | 视频聚合[_aggregate.json] · per_video.mixkit_4000 | {"frames": 401, "dist": {"SAFE": 3, "WARNING": 345, "DANGER": 53}, "switches": 3} | `results/rerun/_aggregate.json` | _aggregate.json |
| 84 | 视频聚合[_aggregate.json] · per_video.mixkit_4026 | {"frames": 237, "dist": {"SAFE": 167, "WARNING": 70, "DANGER": 0}, "switches": 12} | `results/rerun/_aggregate.json` | _aggregate.json |
| 85 | 视频聚合[_aggregate.json] · per_video.mixkit_40656 | {"frames": 288, "dist": {"SAFE": 288, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 86 | 视频聚合[_aggregate.json] · per_video.mixkit_4067 | {"frames": 149, "dist": {"SAFE": 149, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 87 | 视频聚合[_aggregate.json] · per_video.mixkit_4108 | {"frames": 439, "dist": {"SAFE": 263, "WARNING": 176, "DANGER": 0}, "switches": 23} | `results/rerun/_aggregate.json` | _aggregate.json |
| 88 | 视频聚合[_aggregate.json] · per_video.mixkit_4147 | {"frames": 471, "dist": {"SAFE": 2, "WARNING": 50, "DANGER": 419}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 89 | 视频聚合[_aggregate.json] · per_video.mixkit_41537 | {"frames": 695, "dist": {"SAFE": 695, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 90 | 视频聚合[_aggregate.json] · per_video.mixkit_4154 | {"frames": 150, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 145}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 91 | 视频聚合[_aggregate.json] · per_video.mixkit_41574 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 92 | 视频聚合[_aggregate.json] · per_video.mixkit_41576 | {"frames": 1200, "dist": {"SAFE": 1200, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 93 | 视频聚合[_aggregate.json] · per_video.mixkit_4157 | {"frames": 493, "dist": {"SAFE": 2, "WARNING": 50, "DANGER": 441}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 94 | 视频聚合[_aggregate.json] · per_video.mixkit_41581 | {"frames": 314, "dist": {"SAFE": 234, "WARNING": 80, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 95 | 视频聚合[_aggregate.json] · per_video.mixkit_4178 | {"frames": 298, "dist": {"SAFE": 13, "WARNING": 167, "DANGER": 118}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 96 | 视频聚合[_aggregate.json] · per_video.mixkit_4180 | {"frames": 278, "dist": {"SAFE": 2, "WARNING": 6, "DANGER": 270}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 97 | 视频聚合[_aggregate.json] · per_video.mixkit_4185 | {"frames": 156, "dist": {"SAFE": 2, "WARNING": 27, "DANGER": 127}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 98 | 视频聚合[_aggregate.json] · per_video.mixkit_4186 | {"frames": 117, "dist": {"SAFE": 2, "WARNING": 31, "DANGER": 84}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 99 | 视频聚合[_aggregate.json] · per_video.mixkit_4188 | {"frames": 586, "dist": {"SAFE": 2, "WARNING": 78, "DANGER": 506}, "switches": 14} | `results/rerun/_aggregate.json` | _aggregate.json |
| 100 | 视频聚合[_aggregate.json] · per_video.mixkit_42121 | {"frames": 216, "dist": {"SAFE": 146, "WARNING": 70, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 101 | 视频聚合[_aggregate.json] · per_video.mixkit_42123 | {"frames": 359, "dist": {"SAFE": 161, "WARNING": 198, "DANGER": 0}, "switches": 13} | `results/rerun/_aggregate.json` | _aggregate.json |
| 102 | 视频聚合[_aggregate.json] · per_video.mixkit_42130 | {"frames": 262, "dist": {"SAFE": 172, "WARNING": 60, "DANGER": 30}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 103 | 视频聚合[_aggregate.json] · per_video.mixkit_42133 | {"frames": 253, "dist": {"SAFE": 100, "WARNING": 93, "DANGER": 60}, "switches": 12} | `results/rerun/_aggregate.json` | _aggregate.json |
| 104 | 视频聚合[_aggregate.json] · per_video.mixkit_42135 | {"frames": 193, "dist": {"SAFE": 159, "WARNING": 34, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 105 | 视频聚合[_aggregate.json] · per_video.mixkit_42136 | {"frames": 232, "dist": {"SAFE": 88, "WARNING": 81, "DANGER": 63}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 106 | 视频聚合[_aggregate.json] · per_video.mixkit_42137 | {"frames": 325, "dist": {"SAFE": 270, "WARNING": 55, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 107 | 视频聚合[_aggregate.json] · per_video.mixkit_4231 | {"frames": 474, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 469}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 108 | 视频聚合[_aggregate.json] · per_video.mixkit_4269 | {"frames": 210, "dist": {"SAFE": 75, "WARNING": 101, "DANGER": 34}, "switches": 10} | `results/rerun/_aggregate.json` | _aggregate.json |
| 109 | 视频聚合[_aggregate.json] · per_video.mixkit_43150 | {"frames": 246, "dist": {"SAFE": 15, "WARNING": 39, "DANGER": 192}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 110 | 视频聚合[_aggregate.json] · per_video.mixkit_4331 | {"frames": 447, "dist": {"SAFE": 435, "WARNING": 12, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 111 | 视频聚合[_aggregate.json] · per_video.mixkit_4332 | {"frames": 596, "dist": {"SAFE": 2, "WARNING": 90, "DANGER": 504}, "switches": 10} | `results/rerun/_aggregate.json` | _aggregate.json |
| 112 | 视频聚合[_aggregate.json] · per_video.mixkit_4344 | {"frames": 388, "dist": {"SAFE": 123, "WARNING": 128, "DANGER": 137}, "switches": 16} | `results/rerun/_aggregate.json` | _aggregate.json |
| 113 | 视频聚合[_aggregate.json] · per_video.mixkit_4348 | {"frames": 553, "dist": {"SAFE": 274, "WARNING": 121, "DANGER": 158}, "switches": 14} | `results/rerun/_aggregate.json` | _aggregate.json |
| 114 | 视频聚合[_aggregate.json] · per_video.mixkit_4384 | {"frames": 185, "dist": {"SAFE": 66, "WARNING": 62, "DANGER": 57}, "switches": 5} | `results/rerun/_aggregate.json` | _aggregate.json |
| 115 | 视频聚合[_aggregate.json] · per_video.mixkit_4390 | {"frames": 120, "dist": {"SAFE": 2, "WARNING": 40, "DANGER": 78}, "switches": 3} | `results/rerun/_aggregate.json` | _aggregate.json |
| 116 | 视频聚合[_aggregate.json] · per_video.mixkit_4401 | {"frames": 594, "dist": {"SAFE": 2, "WARNING": 274, "DANGER": 318}, "switches": 11} | `results/rerun/_aggregate.json` | _aggregate.json |
| 117 | 视频聚合[_aggregate.json] · per_video.mixkit_4437 | {"frames": 937, "dist": {"SAFE": 2, "WARNING": 355, "DANGER": 580}, "switches": 17} | `results/rerun/_aggregate.json` | _aggregate.json |
| 118 | 视频聚合[_aggregate.json] · per_video.mixkit_4450 | {"frames": 609, "dist": {"SAFE": 609, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 119 | 视频聚合[_aggregate.json] · per_video.mixkit_4451 | {"frames": 559, "dist": {"SAFE": 41, "WARNING": 435, "DANGER": 83}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 120 | 视频聚合[_aggregate.json] · per_video.mixkit_4519 | {"frames": 603, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 598}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 121 | 视频聚合[_aggregate.json] · per_video.mixkit_4530 | {"frames": 539, "dist": {"SAFE": 2, "WARNING": 537, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate.json` | _aggregate.json |
| 122 | 视频聚合[_aggregate.json] · per_video.mixkit_4547 | {"frames": 848, "dist": {"SAFE": 409, "WARNING": 439, "DANGER": 0}, "switches": 14} | `results/rerun/_aggregate.json` | _aggregate.json |
| 123 | 视频聚合[_aggregate.json] · per_video.mixkit_4600 | {"frames": 284, "dist": {"SAFE": 284, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 124 | 视频聚合[_aggregate.json] · per_video.mixkit_4629 | {"frames": 177, "dist": {"SAFE": 147, "WARNING": 30, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 125 | 视频聚合[_aggregate.json] · per_video.mixkit_4633 | {"frames": 552, "dist": {"SAFE": 543, "WARNING": 9, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 126 | 视频聚合[_aggregate.json] · per_video.mixkit_4642 | {"frames": 460, "dist": {"SAFE": 384, "WARNING": 47, "DANGER": 29}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 127 | 视频聚合[_aggregate.json] · per_video.mixkit_4653 | {"frames": 151, "dist": {"SAFE": 151, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 128 | 视频聚合[_aggregate.json] · per_video.mixkit_4661 | {"frames": 230, "dist": {"SAFE": 4, "WARNING": 197, "DANGER": 29}, "switches": 3} | `results/rerun/_aggregate.json` | _aggregate.json |
| 129 | 视频聚合[_aggregate.json] · per_video.mixkit_46788 | {"frames": 344, "dist": {"SAFE": 285, "WARNING": 59, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 130 | 视频聚合[_aggregate.json] · per_video.mixkit_47012 | {"frames": 456, "dist": {"SAFE": 413, "WARNING": 43, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 131 | 视频聚合[_aggregate.json] · per_video.mixkit_47213 | {"frames": 1515, "dist": {"SAFE": 1515, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 132 | 视频聚合[_aggregate.json] · per_video.mixkit_4742 | {"frames": 299, "dist": {"SAFE": 299, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 133 | 视频聚合[_aggregate.json] · per_video.mixkit_47673 | {"frames": 435, "dist": {"SAFE": 432, "WARNING": 3, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate.json` | _aggregate.json |
| 134 | 视频聚合[_aggregate.json] · per_video.mixkit_47792 | {"frames": 425, "dist": {"SAFE": 425, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 135 | 视频聚合[_aggregate.json] · per_video.mixkit_4800 | {"frames": 439, "dist": {"SAFE": 428, "WARNING": 11, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 136 | 视频聚合[_aggregate.json] · per_video.mixkit_4823 | {"frames": 309, "dist": {"SAFE": 309, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 137 | 视频聚合[_aggregate.json] · per_video.mixkit_4832 | {"frames": 249, "dist": {"SAFE": 249, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 138 | 视频聚合[_aggregate.json] · per_video.mixkit_4840 | {"frames": 368, "dist": {"SAFE": 368, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 139 | 视频聚合[_aggregate.json] · per_video.mixkit_48507 | {"frames": 646, "dist": {"SAFE": 37, "WARNING": 239, "DANGER": 370}, "switches": 23} | `results/rerun/_aggregate.json` | _aggregate.json |
| 140 | 视频聚合[_aggregate.json] · per_video.mixkit_48509 | {"frames": 333, "dist": {"SAFE": 68, "WARNING": 236, "DANGER": 29}, "switches": 13} | `results/rerun/_aggregate.json` | _aggregate.json |
| 141 | 视频聚合[_aggregate.json] · per_video.mixkit_48510 | {"frames": 274, "dist": {"SAFE": 141, "WARNING": 71, "DANGER": 62}, "switches": 15} | `results/rerun/_aggregate.json` | _aggregate.json |
| 142 | 视频聚合[_aggregate.json] · per_video.mixkit_4855 | {"frames": 356, "dist": {"SAFE": 294, "WARNING": 62, "DANGER": 0}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 143 | 视频聚合[_aggregate.json] · per_video.mixkit_4856 | {"frames": 412, "dist": {"SAFE": 412, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 144 | 视频聚合[_aggregate.json] · per_video.mixkit_4893 | {"frames": 161, "dist": {"SAFE": 161, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 145 | 视频聚合[_aggregate.json] · per_video.mixkit_49137 | {"frames": 595, "dist": {"SAFE": 586, "WARNING": 9, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 146 | 视频聚合[_aggregate.json] · per_video.mixkit_4915 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 147 | 视频聚合[_aggregate.json] · per_video.mixkit_4948 | {"frames": 247, "dist": {"SAFE": 2, "WARNING": 120, "DANGER": 125}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 148 | 视频聚合[_aggregate.json] · per_video.mixkit_49845 | {"frames": 501, "dist": {"SAFE": 501, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 149 | 视频聚合[_aggregate.json] · per_video.mixkit_49846 | {"frames": 1049, "dist": {"SAFE": 1049, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 150 | 视频聚合[_aggregate.json] · per_video.mixkit_49878 | {"frames": 343, "dist": {"SAFE": 343, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 151 | 视频聚合[_aggregate.json] · per_video.mixkit_50134 | {"frames": 388, "dist": {"SAFE": 388, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 152 | 视频聚合[_aggregate.json] · per_video.mixkit_513 | {"frames": 614, "dist": {"SAFE": 606, "WARNING": 8, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate.json` | _aggregate.json |
| 153 | 视频聚合[_aggregate.json] · per_video.mixkit_52427 | {"frames": 177, "dist": {"SAFE": 177, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 154 | 视频聚合[_aggregate.json] · per_video.mixkit_52454 | {"frames": 744, "dist": {"SAFE": 744, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 155 | 视频聚合[_aggregate.json] · per_video.mixkit_5396 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 156 | 视频聚合[_aggregate.json] · per_video.mixkit_5944 | {"frames": 704, "dist": {"SAFE": 704, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 157 | 视频聚合[_aggregate.json] · per_video.mixkit_6302 | {"frames": 487, "dist": {"SAFE": 52, "WARNING": 285, "DANGER": 150}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 158 | 视频聚合[_aggregate.json] · per_video.mixkit_805 | {"frames": 404, "dist": {"SAFE": 124, "WARNING": 147, "DANGER": 133}, "switches": 13} | `results/rerun/_aggregate.json` | _aggregate.json |
| 159 | 视频聚合[_aggregate.json] · per_video.mixkit_8739 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 160 | 视频聚合[_aggregate.json] · per_video.mixkit_8869 | {"frames": 257, "dist": {"SAFE": 123, "WARNING": 134, "DANGER": 0}, "switches": 6} | `results/rerun/_aggregate.json` | _aggregate.json |
| 161 | 视频聚合[_aggregate.json] · per_video.mixkit_9060 | {"frames": 175, "dist": {"SAFE": 16, "WARNING": 130, "DANGER": 29}, "switches": 4} | `results/rerun/_aggregate.json` | _aggregate.json |
| 162 | 视频聚合[_aggregate.json] · per_video.mixkit_9607 | {"frames": 839, "dist": {"SAFE": 839, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 163 | 视频聚合[_aggregate.json] · per_video.mixkit_991 | {"frames": 594, "dist": {"SAFE": 594, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate.json` | _aggregate.json |
| 164 | 视频聚合[_aggregate.json] · per_video.mixkit_992 | {"frames": 281, "dist": {"SAFE": 124, "WARNING": 157, "DANGER": 0}, "switches": 8} | `results/rerun/_aggregate.json` | _aggregate.json |
| 165 | 视频聚合[_aggregate.json] · per_video.mixkit_993 | {"frames": 975, "dist": {"SAFE": 860, "WARNING": 115, "DANGER": 0}, "switches": 16} | `results/rerun/_aggregate.json` | _aggregate.json |
| 166 | 视频聚合[_aggregate.json] · _comment | 2026-10-30 23:26 lead: 覆盖为从当前逐帧 timeline 重算的聚合值；旧值(SAFE26885/WARNING6388/DANGER11748/650switches)无法从当前 timeline 复现，判定为过期残留。canonical: _aggregate_recomputed.json | `results/rerun/_aggregate.json` | _aggregate.json |
| 167 | 视频聚合[_aggregate_recomputed.json] · total_videos | 111 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 168 | 视频聚合[_aggregate_recomputed.json] · total_frames | 45021 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 169 | 视频聚合[_aggregate_recomputed.json] · levels.SAFE | 28214 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 170 | 视频聚合[_aggregate_recomputed.json] · levels.WARNING | 8355 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 171 | 视频聚合[_aggregate_recomputed.json] · levels.DANGER | 8452 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 172 | 视频聚合[_aggregate_recomputed.json] · switches.list | [4, 0, 12, 13, 2, 1, 11, 7, 6, 16, 0, 4, 6, 8, 6, 2, 26, 0, 4, 4, 1, 7, 8, 2, 2, 6, 8, 2, 3, 12, 0, 0, 23, 6, 0, 2, 0, 0, 4, 4, 6, 2, 4, 4, 14, 4, 13, 8, 12, 2, 8, 4, 2, 10, 8, 2, 10, 16, 14, 5, 3, 11 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 173 | 视频聚合[_aggregate_recomputed.json] · switches.total | 568 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 174 | 视频聚合[_aggregate_recomputed.json] · switches.mean | 5.1171 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 175 | 视频聚合[_aggregate_recomputed.json] · switches.median | 4.0 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 176 | 视频聚合[_aggregate_recomputed.json] · switches.min | 0 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 177 | 视频聚合[_aggregate_recomputed.json] · switches.max | 26 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 178 | 视频聚合[_aggregate_recomputed.json] · videos_with_danger | 45 | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 179 | 视频聚合[_aggregate_recomputed.json] · fiedler_by_level.SAFE | {"count": 28214, "mean": 0.0213, "median": 0.0, "std": 0.2033, "positive_count": 427, "positive_mean": 1.4053} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 180 | 视频聚合[_aggregate_recomputed.json] · fiedler_by_level.WARNING | {"count": 8355, "mean": 0.567, "median": 0.0, "std": 0.875, "positive_count": 4027, "positive_mean": 1.1764} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 181 | 视频聚合[_aggregate_recomputed.json] · fiedler_by_level.DANGER | {"count": 8452, "mean": 1.0842, "median": 0.716, "std": 1.1904, "positive_count": 6360, "positive_mean": 1.4409} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 182 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_13192 | {"frames": 305, "dist": {"SAFE": 6, "WARNING": 120, "DANGER": 179}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 183 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_13265 | {"frames": 281, "dist": {"SAFE": 281, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 184 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_134 | {"frames": 600, "dist": {"SAFE": 294, "WARNING": 69, "DANGER": 237}, "switches": 12} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 185 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_14084 | {"frames": 255, "dist": {"SAFE": 67, "WARNING": 159, "DANGER": 29}, "switches": 13} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 186 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_151 | {"frames": 336, "dist": {"SAFE": 326, "WARNING": 10, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 187 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_15914 | {"frames": 294, "dist": {"SAFE": 231, "WARNING": 63, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 188 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_15997 | {"frames": 712, "dist": {"SAFE": 585, "WARNING": 127, "DANGER": 0}, "switches": 11} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 189 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_17630 | {"frames": 282, "dist": {"SAFE": 7, "WARNING": 170, "DANGER": 105}, "switches": 7} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 190 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_18264 | {"frames": 240, "dist": {"SAFE": 34, "WARNING": 33, "DANGER": 173}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 191 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_18272 | {"frames": 486, "dist": {"SAFE": 413, "WARNING": 73, "DANGER": 0}, "switches": 16} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 192 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_18305 | {"frames": 383, "dist": {"SAFE": 383, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 193 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_21326 | {"frames": 301, "dist": {"SAFE": 283, "WARNING": 18, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 194 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_221 | {"frames": 482, "dist": {"SAFE": 32, "WARNING": 93, "DANGER": 357}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 195 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_22427 | {"frames": 217, "dist": {"SAFE": 140, "WARNING": 45, "DANGER": 32}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 196 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_23168 | {"frames": 180, "dist": {"SAFE": 141, "WARNING": 39, "DANGER": 0}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 197 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_23327 | {"frames": 247, "dist": {"SAFE": 232, "WARNING": 15, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 198 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_23410 | {"frames": 703, "dist": {"SAFE": 486, "WARNING": 118, "DANGER": 99}, "switches": 26} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 199 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_23818 | {"frames": 217, "dist": {"SAFE": 217, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 200 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_241 | {"frames": 379, "dist": {"SAFE": 316, "WARNING": 63, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 201 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_25516 | {"frames": 222, "dist": {"SAFE": 180, "WARNING": 8, "DANGER": 34}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 202 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_25890 | {"frames": 309, "dist": {"SAFE": 4, "WARNING": 305, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 203 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_28616 | {"frames": 152, "dist": {"SAFE": 5, "WARNING": 75, "DANGER": 72}, "switches": 7} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 204 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_31086 | {"frames": 202, "dist": {"SAFE": 154, "WARNING": 48, "DANGER": 0}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 205 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_32622 | {"frames": 281, "dist": {"SAFE": 273, "WARNING": 8, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 206 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_3428 | {"frames": 456, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 451}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 207 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_3429 | {"frames": 246, "dist": {"SAFE": 7, "WARNING": 35, "DANGER": 204}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 208 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_3469 | {"frames": 295, "dist": {"SAFE": 79, "WARNING": 216, "DANGER": 0}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 209 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_39767 | {"frames": 249, "dist": {"SAFE": 232, "WARNING": 17, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 210 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4000 | {"frames": 401, "dist": {"SAFE": 3, "WARNING": 345, "DANGER": 53}, "switches": 3} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 211 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4026 | {"frames": 237, "dist": {"SAFE": 167, "WARNING": 70, "DANGER": 0}, "switches": 12} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 212 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_40656 | {"frames": 288, "dist": {"SAFE": 288, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 213 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4067 | {"frames": 149, "dist": {"SAFE": 149, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 214 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4108 | {"frames": 439, "dist": {"SAFE": 263, "WARNING": 176, "DANGER": 0}, "switches": 23} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 215 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4147 | {"frames": 471, "dist": {"SAFE": 2, "WARNING": 50, "DANGER": 419}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 216 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_41537 | {"frames": 695, "dist": {"SAFE": 695, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 217 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4154 | {"frames": 150, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 145}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 218 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_41574 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 219 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_41576 | {"frames": 1200, "dist": {"SAFE": 1200, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 220 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4157 | {"frames": 493, "dist": {"SAFE": 2, "WARNING": 50, "DANGER": 441}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 221 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_41581 | {"frames": 314, "dist": {"SAFE": 234, "WARNING": 80, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 222 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4178 | {"frames": 298, "dist": {"SAFE": 13, "WARNING": 167, "DANGER": 118}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 223 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4180 | {"frames": 278, "dist": {"SAFE": 2, "WARNING": 6, "DANGER": 270}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 224 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4185 | {"frames": 156, "dist": {"SAFE": 2, "WARNING": 27, "DANGER": 127}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 225 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4186 | {"frames": 117, "dist": {"SAFE": 2, "WARNING": 31, "DANGER": 84}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 226 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4188 | {"frames": 586, "dist": {"SAFE": 2, "WARNING": 78, "DANGER": 506}, "switches": 14} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 227 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_42121 | {"frames": 216, "dist": {"SAFE": 146, "WARNING": 70, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 228 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_42123 | {"frames": 359, "dist": {"SAFE": 161, "WARNING": 198, "DANGER": 0}, "switches": 13} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 229 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_42130 | {"frames": 262, "dist": {"SAFE": 172, "WARNING": 60, "DANGER": 30}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 230 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_42133 | {"frames": 253, "dist": {"SAFE": 100, "WARNING": 93, "DANGER": 60}, "switches": 12} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 231 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_42135 | {"frames": 193, "dist": {"SAFE": 159, "WARNING": 34, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 232 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_42136 | {"frames": 232, "dist": {"SAFE": 88, "WARNING": 81, "DANGER": 63}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 233 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_42137 | {"frames": 325, "dist": {"SAFE": 270, "WARNING": 55, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 234 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4231 | {"frames": 474, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 469}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 235 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4269 | {"frames": 210, "dist": {"SAFE": 75, "WARNING": 101, "DANGER": 34}, "switches": 10} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 236 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_43150 | {"frames": 246, "dist": {"SAFE": 15, "WARNING": 39, "DANGER": 192}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 237 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4331 | {"frames": 447, "dist": {"SAFE": 435, "WARNING": 12, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 238 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4332 | {"frames": 596, "dist": {"SAFE": 2, "WARNING": 90, "DANGER": 504}, "switches": 10} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 239 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4344 | {"frames": 388, "dist": {"SAFE": 123, "WARNING": 128, "DANGER": 137}, "switches": 16} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 240 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4348 | {"frames": 553, "dist": {"SAFE": 274, "WARNING": 121, "DANGER": 158}, "switches": 14} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 241 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4384 | {"frames": 185, "dist": {"SAFE": 66, "WARNING": 62, "DANGER": 57}, "switches": 5} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 242 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4390 | {"frames": 120, "dist": {"SAFE": 2, "WARNING": 40, "DANGER": 78}, "switches": 3} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 243 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4401 | {"frames": 594, "dist": {"SAFE": 2, "WARNING": 274, "DANGER": 318}, "switches": 11} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 244 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4437 | {"frames": 937, "dist": {"SAFE": 2, "WARNING": 355, "DANGER": 580}, "switches": 17} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 245 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4450 | {"frames": 609, "dist": {"SAFE": 609, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 246 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4451 | {"frames": 559, "dist": {"SAFE": 41, "WARNING": 435, "DANGER": 83}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 247 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4519 | {"frames": 603, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 598}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 248 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4530 | {"frames": 539, "dist": {"SAFE": 2, "WARNING": 537, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 249 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4547 | {"frames": 848, "dist": {"SAFE": 409, "WARNING": 439, "DANGER": 0}, "switches": 14} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 250 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4600 | {"frames": 284, "dist": {"SAFE": 284, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 251 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4629 | {"frames": 177, "dist": {"SAFE": 147, "WARNING": 30, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 252 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4633 | {"frames": 552, "dist": {"SAFE": 543, "WARNING": 9, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 253 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4642 | {"frames": 460, "dist": {"SAFE": 384, "WARNING": 47, "DANGER": 29}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 254 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4653 | {"frames": 151, "dist": {"SAFE": 151, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 255 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4661 | {"frames": 230, "dist": {"SAFE": 4, "WARNING": 197, "DANGER": 29}, "switches": 3} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 256 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_46788 | {"frames": 344, "dist": {"SAFE": 285, "WARNING": 59, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 257 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_47012 | {"frames": 456, "dist": {"SAFE": 413, "WARNING": 43, "DANGER": 0}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 258 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_47213 | {"frames": 1515, "dist": {"SAFE": 1515, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 259 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4742 | {"frames": 299, "dist": {"SAFE": 299, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 260 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_47673 | {"frames": 435, "dist": {"SAFE": 432, "WARNING": 3, "DANGER": 0}, "switches": 1} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 261 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_47792 | {"frames": 425, "dist": {"SAFE": 425, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 262 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4800 | {"frames": 439, "dist": {"SAFE": 428, "WARNING": 11, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 263 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4823 | {"frames": 309, "dist": {"SAFE": 309, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 264 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4832 | {"frames": 249, "dist": {"SAFE": 249, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 265 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4840 | {"frames": 368, "dist": {"SAFE": 368, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 266 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_48507 | {"frames": 646, "dist": {"SAFE": 37, "WARNING": 239, "DANGER": 370}, "switches": 23} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 267 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_48509 | {"frames": 333, "dist": {"SAFE": 68, "WARNING": 236, "DANGER": 29}, "switches": 13} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 268 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_48510 | {"frames": 274, "dist": {"SAFE": 141, "WARNING": 71, "DANGER": 62}, "switches": 15} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 269 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4855 | {"frames": 356, "dist": {"SAFE": 294, "WARNING": 62, "DANGER": 0}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 270 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4856 | {"frames": 412, "dist": {"SAFE": 412, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 271 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4893 | {"frames": 161, "dist": {"SAFE": 161, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 272 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_49137 | {"frames": 595, "dist": {"SAFE": 586, "WARNING": 9, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 273 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4915 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 274 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_4948 | {"frames": 247, "dist": {"SAFE": 2, "WARNING": 120, "DANGER": 125}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 275 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_49845 | {"frames": 501, "dist": {"SAFE": 501, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 276 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_49846 | {"frames": 1049, "dist": {"SAFE": 1049, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 277 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_49878 | {"frames": 343, "dist": {"SAFE": 343, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 278 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_50134 | {"frames": 388, "dist": {"SAFE": 388, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 279 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_513 | {"frames": 614, "dist": {"SAFE": 606, "WARNING": 8, "DANGER": 0}, "switches": 2} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 280 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_52427 | {"frames": 177, "dist": {"SAFE": 177, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 281 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_52454 | {"frames": 744, "dist": {"SAFE": 744, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 282 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_5396 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 283 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_5944 | {"frames": 704, "dist": {"SAFE": 704, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 284 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_6302 | {"frames": 487, "dist": {"SAFE": 52, "WARNING": 285, "DANGER": 150}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 285 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_805 | {"frames": 404, "dist": {"SAFE": 124, "WARNING": 147, "DANGER": 133}, "switches": 13} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 286 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_8739 | {"frames": 360, "dist": {"SAFE": 360, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 287 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_8869 | {"frames": 257, "dist": {"SAFE": 123, "WARNING": 134, "DANGER": 0}, "switches": 6} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 288 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_9060 | {"frames": 175, "dist": {"SAFE": 16, "WARNING": 130, "DANGER": 29}, "switches": 4} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 289 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_9607 | {"frames": 839, "dist": {"SAFE": 839, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 290 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_991 | {"frames": 594, "dist": {"SAFE": 594, "WARNING": 0, "DANGER": 0}, "switches": 0} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 291 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_992 | {"frames": 281, "dist": {"SAFE": 124, "WARNING": 157, "DANGER": 0}, "switches": 8} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 292 | 视频聚合[_aggregate_recomputed.json] · per_video.mixkit_993 | {"frames": 975, "dist": {"SAFE": 860, "WARNING": 115, "DANGER": 0}, "switches": 16} | `results/rerun/_aggregate_recomputed.json` | _aggregate_recomputed.json |
| 293 | 视频汇总[video_enhanced] · total | 111 | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 294 | 视频汇总[video_enhanced] · total_seconds | 710.5 | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 295 | 视频汇总[video_enhanced] · results.mixkit_13192 | {"status": "ok", "seconds": 9.6, "timeline": "results/rerun/video_enhanced\\mixkit_13192\\mixkit_13192_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 296 | 视频汇总[video_enhanced] · results.mixkit_13265 | {"status": "ok", "seconds": 4.2, "timeline": "results/rerun/video_enhanced\\mixkit_13265\\mixkit_13265_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 297 | 视频汇总[video_enhanced] · results.mixkit_134 | {"status": "ok", "seconds": 9.1, "timeline": "results/rerun/video_enhanced\\mixkit_134\\mixkit_134_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 298 | 视频汇总[video_enhanced] · results.mixkit_14084 | {"status": "ok", "seconds": 4.1, "timeline": "results/rerun/video_enhanced\\mixkit_14084\\mixkit_14084_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 299 | 视频汇总[video_enhanced] · results.mixkit_151 | {"status": "ok", "seconds": 5.3, "timeline": "results/rerun/video_enhanced\\mixkit_151\\mixkit_151_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 300 | 视频汇总[video_enhanced] · results.mixkit_15914 | {"status": "ok", "seconds": 4.6, "timeline": "results/rerun/video_enhanced\\mixkit_15914\\mixkit_15914_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 301 | 视频汇总[video_enhanced] · results.mixkit_15997 | {"status": "ok", "seconds": 11.9, "timeline": "results/rerun/video_enhanced\\mixkit_15997\\mixkit_15997_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 302 | 视频汇总[video_enhanced] · results.mixkit_17630 | {"status": "ok", "seconds": 5.0, "timeline": "results/rerun/video_enhanced\\mixkit_17630\\mixkit_17630_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 303 | 视频汇总[video_enhanced] · results.mixkit_18264 | {"status": "ok", "seconds": 3.8, "timeline": "results/rerun/video_enhanced\\mixkit_18264\\mixkit_18264_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 304 | 视频汇总[video_enhanced] · results.mixkit_18272 | {"status": "ok", "seconds": 7.7, "timeline": "results/rerun/video_enhanced\\mixkit_18272\\mixkit_18272_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 305 | 视频汇总[video_enhanced] · results.mixkit_18305 | {"status": "ok", "seconds": 5.2, "timeline": "results/rerun/video_enhanced\\mixkit_18305\\mixkit_18305_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 306 | 视频汇总[video_enhanced] · results.mixkit_21326 | {"status": "ok", "seconds": 4.6, "timeline": "results/rerun/video_enhanced\\mixkit_21326\\mixkit_21326_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 307 | 视频汇总[video_enhanced] · results.mixkit_221 | {"status": "ok", "seconds": 7.8, "timeline": "results/rerun/video_enhanced\\mixkit_221\\mixkit_221_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 308 | 视频汇总[video_enhanced] · results.mixkit_22427 | {"status": "ok", "seconds": 3.7, "timeline": "results/rerun/video_enhanced\\mixkit_22427\\mixkit_22427_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 309 | 视频汇总[video_enhanced] · results.mixkit_23168 | {"status": "ok", "seconds": 3.1, "timeline": "results/rerun/video_enhanced\\mixkit_23168\\mixkit_23168_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 310 | 视频汇总[video_enhanced] · results.mixkit_23327 | {"status": "ok", "seconds": 4.1, "timeline": "results/rerun/video_enhanced\\mixkit_23327\\mixkit_23327_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 311 | 视频汇总[video_enhanced] · results.mixkit_23410 | {"status": "ok", "seconds": 11.0, "timeline": "results/rerun/video_enhanced\\mixkit_23410\\mixkit_23410_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 312 | 视频汇总[video_enhanced] · results.mixkit_23818 | {"status": "ok", "seconds": 3.2, "timeline": "results/rerun/video_enhanced\\mixkit_23818\\mixkit_23818_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 313 | 视频汇总[video_enhanced] · results.mixkit_241 | {"status": "ok", "seconds": 6.6, "timeline": "results/rerun/video_enhanced\\mixkit_241\\mixkit_241_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 314 | 视频汇总[video_enhanced] · results.mixkit_25516 | {"status": "ok", "seconds": 3.6, "timeline": "results/rerun/video_enhanced\\mixkit_25516\\mixkit_25516_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 315 | 视频汇总[video_enhanced] · results.mixkit_25890 | {"status": "ok", "seconds": 5.3, "timeline": "results/rerun/video_enhanced\\mixkit_25890\\mixkit_25890_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 316 | 视频汇总[video_enhanced] · results.mixkit_28616 | {"status": "ok", "seconds": 2.4, "timeline": "results/rerun/video_enhanced\\mixkit_28616\\mixkit_28616_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 317 | 视频汇总[video_enhanced] · results.mixkit_31086 | {"status": "ok", "seconds": 2.9, "timeline": "results/rerun/video_enhanced\\mixkit_31086\\mixkit_31086_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 318 | 视频汇总[video_enhanced] · results.mixkit_32622 | {"status": "ok", "seconds": 4.2, "timeline": "results/rerun/video_enhanced\\mixkit_32622\\mixkit_32622_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 319 | 视频汇总[video_enhanced] · results.mixkit_3428 | {"status": "ok", "seconds": 9.2, "timeline": "results/rerun/video_enhanced\\mixkit_3428\\mixkit_3428_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 320 | 视频汇总[video_enhanced] · results.mixkit_3429 | {"status": "ok", "seconds": 4.0, "timeline": "results/rerun/video_enhanced\\mixkit_3429\\mixkit_3429_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 321 | 视频汇总[video_enhanced] · results.mixkit_3469 | {"status": "ok", "seconds": 4.6, "timeline": "results/rerun/video_enhanced\\mixkit_3469\\mixkit_3469_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 322 | 视频汇总[video_enhanced] · results.mixkit_39767 | {"status": "ok", "seconds": 4.2, "timeline": "results/rerun/video_enhanced\\mixkit_39767\\mixkit_39767_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 323 | 视频汇总[video_enhanced] · results.mixkit_4000 | {"status": "ok", "seconds": 7.5, "timeline": "results/rerun/video_enhanced\\mixkit_4000\\mixkit_4000_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 324 | 视频汇总[video_enhanced] · results.mixkit_4026 | {"status": "ok", "seconds": 3.7, "timeline": "results/rerun/video_enhanced\\mixkit_4026\\mixkit_4026_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 325 | 视频汇总[video_enhanced] · results.mixkit_40656 | {"status": "ok", "seconds": 4.3, "timeline": "results/rerun/video_enhanced\\mixkit_40656\\mixkit_40656_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 326 | 视频汇总[video_enhanced] · results.mixkit_4067 | {"status": "ok", "seconds": 2.2, "timeline": "results/rerun/video_enhanced\\mixkit_4067\\mixkit_4067_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 327 | 视频汇总[video_enhanced] · results.mixkit_4108 | {"status": "ok", "seconds": 7.0, "timeline": "results/rerun/video_enhanced\\mixkit_4108\\mixkit_4108_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 328 | 视频汇总[video_enhanced] · results.mixkit_4147 | {"status": "ok", "seconds": 8.2, "timeline": "results/rerun/video_enhanced\\mixkit_4147\\mixkit_4147_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 329 | 视频汇总[video_enhanced] · results.mixkit_41537 | {"status": "ok", "seconds": 9.3, "timeline": "results/rerun/video_enhanced\\mixkit_41537\\mixkit_41537_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 330 | 视频汇总[video_enhanced] · results.mixkit_4154 | {"status": "ok", "seconds": 3.3, "timeline": "results/rerun/video_enhanced\\mixkit_4154\\mixkit_4154_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 331 | 视频汇总[video_enhanced] · results.mixkit_4157 | {"status": "ok", "seconds": 8.3, "timeline": "results/rerun/video_enhanced\\mixkit_4157\\mixkit_4157_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 332 | 视频汇总[video_enhanced] · results.mixkit_41574 | {"status": "ok", "seconds": 5.1, "timeline": "results/rerun/video_enhanced\\mixkit_41574\\mixkit_41574_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 333 | 视频汇总[video_enhanced] · results.mixkit_41576 | {"status": "ok", "seconds": 17.5, "timeline": "results/rerun/video_enhanced\\mixkit_41576\\mixkit_41576_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 334 | 视频汇总[video_enhanced] · results.mixkit_41581 | {"status": "ok", "seconds": 4.9, "timeline": "results/rerun/video_enhanced\\mixkit_41581\\mixkit_41581_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 335 | 视频汇总[video_enhanced] · results.mixkit_4178 | {"status": "ok", "seconds": 5.4, "timeline": "results/rerun/video_enhanced\\mixkit_4178\\mixkit_4178_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 336 | 视频汇总[video_enhanced] · results.mixkit_4180 | {"status": "ok", "seconds": 5.6, "timeline": "results/rerun/video_enhanced\\mixkit_4180\\mixkit_4180_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 337 | 视频汇总[video_enhanced] · results.mixkit_4185 | {"status": "ok", "seconds": 2.6, "timeline": "results/rerun/video_enhanced\\mixkit_4185\\mixkit_4185_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 338 | 视频汇总[video_enhanced] · results.mixkit_4186 | {"status": "ok", "seconds": 1.9, "timeline": "results/rerun/video_enhanced\\mixkit_4186\\mixkit_4186_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 339 | 视频汇总[video_enhanced] · results.mixkit_4188 | {"status": "ok", "seconds": 10.5, "timeline": "results/rerun/video_enhanced\\mixkit_4188\\mixkit_4188_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 340 | 视频汇总[video_enhanced] · results.mixkit_42121 | {"status": "ok", "seconds": 3.7, "timeline": "results/rerun/video_enhanced\\mixkit_42121\\mixkit_42121_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 341 | 视频汇总[video_enhanced] · results.mixkit_42123 | {"status": "ok", "seconds": 5.6, "timeline": "results/rerun/video_enhanced\\mixkit_42123\\mixkit_42123_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 342 | 视频汇总[video_enhanced] · results.mixkit_42130 | {"status": "ok", "seconds": 4.4, "timeline": "results/rerun/video_enhanced\\mixkit_42130\\mixkit_42130_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 343 | 视频汇总[video_enhanced] · results.mixkit_42133 | {"status": "ok", "seconds": 4.3, "timeline": "results/rerun/video_enhanced\\mixkit_42133\\mixkit_42133_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 344 | 视频汇总[video_enhanced] · results.mixkit_42135 | {"status": "ok", "seconds": 3.3, "timeline": "results/rerun/video_enhanced\\mixkit_42135\\mixkit_42135_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 345 | 视频汇总[video_enhanced] · results.mixkit_42136 | {"status": "ok", "seconds": 4.0, "timeline": "results/rerun/video_enhanced\\mixkit_42136\\mixkit_42136_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 346 | 视频汇总[video_enhanced] · results.mixkit_42137 | {"status": "ok", "seconds": 5.1, "timeline": "results/rerun/video_enhanced\\mixkit_42137\\mixkit_42137_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 347 | 视频汇总[video_enhanced] · results.mixkit_4231 | {"status": "ok", "seconds": 9.0, "timeline": "results/rerun/video_enhanced\\mixkit_4231\\mixkit_4231_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 348 | 视频汇总[video_enhanced] · results.mixkit_4269 | {"status": "ok", "seconds": 3.5, "timeline": "results/rerun/video_enhanced\\mixkit_4269\\mixkit_4269_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 349 | 视频汇总[video_enhanced] · results.mixkit_43150 | {"status": "ok", "seconds": 3.9, "timeline": "results/rerun/video_enhanced\\mixkit_43150\\mixkit_43150_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 350 | 视频汇总[video_enhanced] · results.mixkit_4331 | {"status": "ok", "seconds": 7.3, "timeline": "results/rerun/video_enhanced\\mixkit_4331\\mixkit_4331_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 351 | 视频汇总[video_enhanced] · results.mixkit_4332 | {"status": "ok", "seconds": 10.6, "timeline": "results/rerun/video_enhanced\\mixkit_4332\\mixkit_4332_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 352 | 视频汇总[video_enhanced] · results.mixkit_4344 | {"status": "ok", "seconds": 5.9, "timeline": "results/rerun/video_enhanced\\mixkit_4344\\mixkit_4344_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 353 | 视频汇总[video_enhanced] · results.mixkit_4348 | {"status": "ok", "seconds": 9.0, "timeline": "results/rerun/video_enhanced\\mixkit_4348\\mixkit_4348_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 354 | 视频汇总[video_enhanced] · results.mixkit_4384 | {"status": "ok", "seconds": 2.5, "timeline": "results/rerun/video_enhanced\\mixkit_4384\\mixkit_4384_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 355 | 视频汇总[video_enhanced] · results.mixkit_4390 | {"status": "ok", "seconds": 1.9, "timeline": "results/rerun/video_enhanced\\mixkit_4390\\mixkit_4390_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 356 | 视频汇总[video_enhanced] · results.mixkit_4401 | {"status": "ok", "seconds": 10.0, "timeline": "results/rerun/video_enhanced\\mixkit_4401\\mixkit_4401_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 357 | 视频汇总[video_enhanced] · results.mixkit_4437 | {"status": "ok", "seconds": 17.0, "timeline": "results/rerun/video_enhanced\\mixkit_4437\\mixkit_4437_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 358 | 视频汇总[video_enhanced] · results.mixkit_4450 | {"status": "ok", "seconds": 8.0, "timeline": "results/rerun/video_enhanced\\mixkit_4450\\mixkit_4450_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 359 | 视频汇总[video_enhanced] · results.mixkit_4451 | {"status": "ok", "seconds": 8.3, "timeline": "results/rerun/video_enhanced\\mixkit_4451\\mixkit_4451_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 360 | 视频汇总[video_enhanced] · results.mixkit_4519 | {"status": "ok", "seconds": 9.3, "timeline": "results/rerun/video_enhanced\\mixkit_4519\\mixkit_4519_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 361 | 视频汇总[video_enhanced] · results.mixkit_4530 | {"status": "ok", "seconds": 8.1, "timeline": "results/rerun/video_enhanced\\mixkit_4530\\mixkit_4530_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 362 | 视频汇总[video_enhanced] · results.mixkit_4547 | {"status": "ok", "seconds": 15.0, "timeline": "results/rerun/video_enhanced\\mixkit_4547\\mixkit_4547_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 363 | 视频汇总[video_enhanced] · results.mixkit_4600 | {"status": "ok", "seconds": 4.5, "timeline": "results/rerun/video_enhanced\\mixkit_4600\\mixkit_4600_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 364 | 视频汇总[video_enhanced] · results.mixkit_4629 | {"status": "ok", "seconds": 2.9, "timeline": "results/rerun/video_enhanced\\mixkit_4629\\mixkit_4629_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 365 | 视频汇总[video_enhanced] · results.mixkit_4633 | {"status": "ok", "seconds": 8.4, "timeline": "results/rerun/video_enhanced\\mixkit_4633\\mixkit_4633_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 366 | 视频汇总[video_enhanced] · results.mixkit_4642 | {"status": "ok", "seconds": 7.2, "timeline": "results/rerun/video_enhanced\\mixkit_4642\\mixkit_4642_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 367 | 视频汇总[video_enhanced] · results.mixkit_4653 | {"status": "ok", "seconds": 2.2, "timeline": "results/rerun/video_enhanced\\mixkit_4653\\mixkit_4653_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 368 | 视频汇总[video_enhanced] · results.mixkit_4661 | {"status": "ok", "seconds": 3.6, "timeline": "results/rerun/video_enhanced\\mixkit_4661\\mixkit_4661_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 369 | 视频汇总[video_enhanced] · results.mixkit_46788 | {"status": "ok", "seconds": 5.1, "timeline": "results/rerun/video_enhanced\\mixkit_46788\\mixkit_46788_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 370 | 视频汇总[video_enhanced] · results.mixkit_47012 | {"status": "ok", "seconds": 6.7, "timeline": "results/rerun/video_enhanced\\mixkit_47012\\mixkit_47012_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 371 | 视频汇总[video_enhanced] · results.mixkit_47213 | {"status": "ok", "seconds": 20.3, "timeline": "results/rerun/video_enhanced\\mixkit_47213\\mixkit_47213_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 372 | 视频汇总[video_enhanced] · results.mixkit_4742 | {"status": "ok", "seconds": 4.2, "timeline": "results/rerun/video_enhanced\\mixkit_4742\\mixkit_4742_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 373 | 视频汇总[video_enhanced] · results.mixkit_47673 | {"status": "ok", "seconds": 6.7, "timeline": "results/rerun/video_enhanced\\mixkit_47673\\mixkit_47673_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 374 | 视频汇总[video_enhanced] · results.mixkit_47792 | {"status": "ok", "seconds": 6.9, "timeline": "results/rerun/video_enhanced\\mixkit_47792\\mixkit_47792_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 375 | 视频汇总[video_enhanced] · results.mixkit_4800 | {"status": "ok", "seconds": 6.1, "timeline": "results/rerun/video_enhanced\\mixkit_4800\\mixkit_4800_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 376 | 视频汇总[video_enhanced] · results.mixkit_4823 | {"status": "ok", "seconds": 4.4, "timeline": "results/rerun/video_enhanced\\mixkit_4823\\mixkit_4823_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 377 | 视频汇总[video_enhanced] · results.mixkit_4832 | {"status": "ok", "seconds": 3.7, "timeline": "results/rerun/video_enhanced\\mixkit_4832\\mixkit_4832_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 378 | 视频汇总[video_enhanced] · results.mixkit_4840 | {"status": "ok", "seconds": 5.4, "timeline": "results/rerun/video_enhanced\\mixkit_4840\\mixkit_4840_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 379 | 视频汇总[video_enhanced] · results.mixkit_48507 | {"status": "ok", "seconds": 11.0, "timeline": "results/rerun/video_enhanced\\mixkit_48507\\mixkit_48507_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 380 | 视频汇总[video_enhanced] · results.mixkit_48509 | {"status": "ok", "seconds": 5.0, "timeline": "results/rerun/video_enhanced\\mixkit_48509\\mixkit_48509_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 381 | 视频汇总[video_enhanced] · results.mixkit_48510 | {"status": "ok", "seconds": 4.3, "timeline": "results/rerun/video_enhanced\\mixkit_48510\\mixkit_48510_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 382 | 视频汇总[video_enhanced] · results.mixkit_4855 | {"status": "ok", "seconds": 6.3, "timeline": "results/rerun/video_enhanced\\mixkit_4855\\mixkit_4855_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 383 | 视频汇总[video_enhanced] · results.mixkit_4856 | {"status": "ok", "seconds": 7.1, "timeline": "results/rerun/video_enhanced\\mixkit_4856\\mixkit_4856_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 384 | 视频汇总[video_enhanced] · results.mixkit_4893 | {"status": "ok", "seconds": 2.5, "timeline": "results/rerun/video_enhanced\\mixkit_4893\\mixkit_4893_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 385 | 视频汇总[video_enhanced] · results.mixkit_49137 | {"status": "ok", "seconds": 9.6, "timeline": "results/rerun/video_enhanced\\mixkit_49137\\mixkit_49137_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 386 | 视频汇总[video_enhanced] · results.mixkit_4915 | {"status": "ok", "seconds": 6.0, "timeline": "results/rerun/video_enhanced\\mixkit_4915\\mixkit_4915_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 387 | 视频汇总[video_enhanced] · results.mixkit_4948 | {"status": "ok", "seconds": 4.2, "timeline": "results/rerun/video_enhanced\\mixkit_4948\\mixkit_4948_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 388 | 视频汇总[video_enhanced] · results.mixkit_49845 | {"status": "ok", "seconds": 6.7, "timeline": "results/rerun/video_enhanced\\mixkit_49845\\mixkit_49845_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 389 | 视频汇总[video_enhanced] · results.mixkit_49846 | {"status": "ok", "seconds": 15.7, "timeline": "results/rerun/video_enhanced\\mixkit_49846\\mixkit_49846_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 390 | 视频汇总[video_enhanced] · results.mixkit_49878 | {"status": "ok", "seconds": 4.3, "timeline": "results/rerun/video_enhanced\\mixkit_49878\\mixkit_49878_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 391 | 视频汇总[video_enhanced] · results.mixkit_50134 | {"status": "ok", "seconds": 5.2, "timeline": "results/rerun/video_enhanced\\mixkit_50134\\mixkit_50134_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 392 | 视频汇总[video_enhanced] · results.mixkit_513 | {"status": "ok", "seconds": 9.1, "timeline": "results/rerun/video_enhanced\\mixkit_513\\mixkit_513_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 393 | 视频汇总[video_enhanced] · results.mixkit_52427 | {"status": "ok", "seconds": 2.6, "timeline": "results/rerun/video_enhanced\\mixkit_52427\\mixkit_52427_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 394 | 视频汇总[video_enhanced] · results.mixkit_52454 | {"status": "ok", "seconds": 9.5, "timeline": "results/rerun/video_enhanced\\mixkit_52454\\mixkit_52454_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 395 | 视频汇总[video_enhanced] · results.mixkit_5396 | {"status": "ok", "seconds": 5.0, "timeline": "results/rerun/video_enhanced\\mixkit_5396\\mixkit_5396_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 396 | 视频汇总[video_enhanced] · results.mixkit_5944 | {"status": "ok", "seconds": 9.9, "timeline": "results/rerun/video_enhanced\\mixkit_5944\\mixkit_5944_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 397 | 视频汇总[video_enhanced] · results.mixkit_6302 | {"status": "ok", "seconds": 7.7, "timeline": "results/rerun/video_enhanced\\mixkit_6302\\mixkit_6302_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 398 | 视频汇总[video_enhanced] · results.mixkit_805 | {"status": "ok", "seconds": 6.1, "timeline": "results/rerun/video_enhanced\\mixkit_805\\mixkit_805_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 399 | 视频汇总[video_enhanced] · results.mixkit_8739 | {"status": "ok", "seconds": 5.2, "timeline": "results/rerun/video_enhanced\\mixkit_8739\\mixkit_8739_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 400 | 视频汇总[video_enhanced] · results.mixkit_8869 | {"status": "ok", "seconds": 4.3, "timeline": "results/rerun/video_enhanced\\mixkit_8869\\mixkit_8869_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 401 | 视频汇总[video_enhanced] · results.mixkit_9060 | {"status": "ok", "seconds": 3.1, "timeline": "results/rerun/video_enhanced\\mixkit_9060\\mixkit_9060_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 402 | 视频汇总[video_enhanced] · results.mixkit_9607 | {"status": "ok", "seconds": 11.2, "timeline": "results/rerun/video_enhanced\\mixkit_9607\\mixkit_9607_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 403 | 视频汇总[video_enhanced] · results.mixkit_991 | {"status": "ok", "seconds": 9.1, "timeline": "results/rerun/video_enhanced\\mixkit_991\\mixkit_991_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 404 | 视频汇总[video_enhanced] · results.mixkit_992 | {"status": "ok", "seconds": 4.7, "timeline": "results/rerun/video_enhanced\\mixkit_992\\mixkit_992_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 405 | 视频汇总[video_enhanced] · results.mixkit_993 | {"status": "ok", "seconds": 14.8, "timeline": "results/rerun/video_enhanced\\mixkit_993\\mixkit_993_timeline.json"} | `results/rerun/video_enhanced/_summary.json` | video_enhanced/_summary.json |
| 406 | 视频汇总[video_baseline] · total | 111 | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 407 | 视频汇总[video_baseline] · results.mixkit_13192 | {"frames": 305, "switches": 48, "level_distribution": {"SAFE": 90, "WARNING": 53, "DANGER": 162}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 408 | 视频汇总[video_baseline] · results.mixkit_13265 | {"frames": 281, "switches": 0, "level_distribution": {"SAFE": 281}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 409 | 视频汇总[video_baseline] · results.mixkit_134 | {"frames": 600, "switches": 0, "level_distribution": {"SAFE": 600}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 410 | 视频汇总[video_baseline] · results.mixkit_14084 | {"frames": 255, "switches": 53, "level_distribution": {"WARNING": 55, "SAFE": 200}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 411 | 视频汇总[video_baseline] · results.mixkit_151 | {"frames": 336, "switches": 0, "level_distribution": {"SAFE": 336}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 412 | 视频汇总[video_baseline] · results.mixkit_15914 | {"frames": 294, "switches": 2, "level_distribution": {"SAFE": 293, "WARNING": 1}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 413 | 视频汇总[video_baseline] · results.mixkit_15997 | {"frames": 712, "switches": 38, "level_distribution": {"SAFE": 684, "WARNING": 28}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 414 | 视频汇总[video_baseline] · results.mixkit_17630 | {"frames": 282, "switches": 116, "level_distribution": {"SAFE": 78, "WARNING": 181, "DANGER": 23}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 415 | 视频汇总[video_baseline] · results.mixkit_18264 | {"frames": 240, "switches": 0, "level_distribution": {"SAFE": 240}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 416 | 视频汇总[video_baseline] · results.mixkit_18272 | {"frames": 486, "switches": 0, "level_distribution": {"SAFE": 486}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 417 | 视频汇总[video_baseline] · results.mixkit_18305 | {"frames": 383, "switches": 0, "level_distribution": {"SAFE": 383}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 418 | 视频汇总[video_baseline] · results.mixkit_21326 | {"frames": 301, "switches": 0, "level_distribution": {"SAFE": 301}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 419 | 视频汇总[video_baseline] · results.mixkit_221 | {"frames": 482, "switches": 0, "level_distribution": {"SAFE": 482}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 420 | 视频汇总[video_baseline] · results.mixkit_22427 | {"frames": 217, "switches": 0, "level_distribution": {"SAFE": 217}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 421 | 视频汇总[video_baseline] · results.mixkit_23168 | {"frames": 180, "switches": 2, "level_distribution": {"SAFE": 179, "WARNING": 1}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 422 | 视频汇总[video_baseline] · results.mixkit_23327 | {"frames": 247, "switches": 0, "level_distribution": {"SAFE": 247}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 423 | 视频汇总[video_baseline] · results.mixkit_23410 | {"frames": 703, "switches": 0, "level_distribution": {"SAFE": 703}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 424 | 视频汇总[video_baseline] · results.mixkit_23818 | {"frames": 217, "switches": 0, "level_distribution": {"SAFE": 217}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 425 | 视频汇总[video_baseline] · results.mixkit_241 | {"frames": 379, "switches": 6, "level_distribution": {"SAFE": 371, "WARNING": 8}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 426 | 视频汇总[video_baseline] · results.mixkit_25516 | {"frames": 222, "switches": 0, "level_distribution": {"SAFE": 222}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 427 | 视频汇总[video_baseline] · results.mixkit_25890 | {"frames": 309, "switches": 29, "level_distribution": {"SAFE": 278, "WARNING": 27, "DANGER": 4}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 428 | 视频汇总[video_baseline] · results.mixkit_28616 | {"frames": 152, "switches": 48, "level_distribution": {"WARNING": 40, "SAFE": 111, "DANGER": 1}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 429 | 视频汇总[video_baseline] · results.mixkit_31086 | {"frames": 202, "switches": 14, "level_distribution": {"SAFE": 191, "WARNING": 11}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 430 | 视频汇总[video_baseline] · results.mixkit_32622 | {"frames": 281, "switches": 0, "level_distribution": {"SAFE": 281}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 431 | 视频汇总[video_baseline] · results.mixkit_3428 | {"frames": 456, "switches": 55, "level_distribution": {"DANGER": 423, "WARNING": 32, "SAFE": 1}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 432 | 视频汇总[video_baseline] · results.mixkit_3429 | {"frames": 246, "switches": 58, "level_distribution": {"SAFE": 141, "WARNING": 81, "DANGER": 24}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 433 | 视频汇总[video_baseline] · results.mixkit_3469 | {"frames": 295, "switches": 56, "level_distribution": {"SAFE": 223, "WARNING": 70, "DANGER": 2}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 434 | 视频汇总[video_baseline] · results.mixkit_39767 | {"frames": 249, "switches": 8, "level_distribution": {"SAFE": 244, "WARNING": 5}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 435 | 视频汇总[video_baseline] · results.mixkit_4000 | {"frames": 401, "switches": 95, "level_distribution": {"SAFE": 322, "WARNING": 77, "DANGER": 2}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 436 | 视频汇总[video_baseline] · results.mixkit_4026 | {"frames": 237, "switches": 4, "level_distribution": {"SAFE": 235, "WARNING": 2}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 437 | 视频汇总[video_baseline] · results.mixkit_40656 | {"frames": 288, "switches": 0, "level_distribution": {"SAFE": 288}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 438 | 视频汇总[video_baseline] · results.mixkit_4067 | {"frames": 149, "switches": 0, "level_distribution": {"SAFE": 149}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 439 | 视频汇总[video_baseline] · results.mixkit_4108 | {"frames": 439, "switches": 0, "level_distribution": {"SAFE": 439}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 440 | 视频汇总[video_baseline] · results.mixkit_4147 | {"frames": 471, "switches": 61, "level_distribution": {"DANGER": 357, "WARNING": 110, "SAFE": 4}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 441 | 视频汇总[video_baseline] · results.mixkit_41537 | {"frames": 695, "switches": 0, "level_distribution": {"SAFE": 695}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 442 | 视频汇总[video_baseline] · results.mixkit_4154 | {"frames": 150, "switches": 16, "level_distribution": {"DANGER": 142, "WARNING": 8}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 443 | 视频汇总[video_baseline] · results.mixkit_4157 | {"frames": 493, "switches": 8, "level_distribution": {"SAFE": 482, "WARNING": 11}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 444 | 视频汇总[video_baseline] · results.mixkit_41574 | {"frames": 360, "switches": 0, "level_distribution": {"SAFE": 360}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 445 | 视频汇总[video_baseline] · results.mixkit_41576 | {"frames": 1200, "switches": 0, "level_distribution": {"SAFE": 1200}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 446 | 视频汇总[video_baseline] · results.mixkit_41581 | {"frames": 314, "switches": 0, "level_distribution": {"SAFE": 314}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 447 | 视频汇总[video_baseline] · results.mixkit_4178 | {"frames": 298, "switches": 96, "level_distribution": {"SAFE": 213, "WARNING": 83, "DANGER": 2}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 448 | 视频汇总[video_baseline] · results.mixkit_4180 | {"frames": 278, "switches": 95, "level_distribution": {"WARNING": 156, "SAFE": 13, "DANGER": 109}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 449 | 视频汇总[video_baseline] · results.mixkit_4185 | {"frames": 156, "switches": 63, "level_distribution": {"WARNING": 70, "DANGER": 25, "SAFE": 61}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 450 | 视频汇总[video_baseline] · results.mixkit_4186 | {"frames": 117, "switches": 45, "level_distribution": {"WARNING": 91, "DANGER": 16, "SAFE": 10}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 451 | 视频汇总[video_baseline] · results.mixkit_4188 | {"frames": 586, "switches": 191, "level_distribution": {"WARNING": 340, "SAFE": 218, "DANGER": 28}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 452 | 视频汇总[video_baseline] · results.mixkit_42121 | {"frames": 216, "switches": 0, "level_distribution": {"SAFE": 216}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 453 | 视频汇总[video_baseline] · results.mixkit_42123 | {"frames": 359, "switches": 0, "level_distribution": {"SAFE": 359}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 454 | 视频汇总[video_baseline] · results.mixkit_42130 | {"frames": 262, "switches": 0, "level_distribution": {"SAFE": 262}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 455 | 视频汇总[video_baseline] · results.mixkit_42133 | {"frames": 253, "switches": 0, "level_distribution": {"SAFE": 253}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 456 | 视频汇总[video_baseline] · results.mixkit_42135 | {"frames": 193, "switches": 0, "level_distribution": {"SAFE": 193}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 457 | 视频汇总[video_baseline] · results.mixkit_42136 | {"frames": 232, "switches": 0, "level_distribution": {"SAFE": 232}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 458 | 视频汇总[video_baseline] · results.mixkit_42137 | {"frames": 325, "switches": 4, "level_distribution": {"SAFE": 323, "WARNING": 2}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 459 | 视频汇总[video_baseline] · results.mixkit_4231 | {"frames": 474, "switches": 134, "level_distribution": {"DANGER": 298, "WARNING": 176}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 460 | 视频汇总[video_baseline] · results.mixkit_4269 | {"frames": 210, "switches": 5, "level_distribution": {"WARNING": 5, "SAFE": 205}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 461 | 视频汇总[video_baseline] · results.mixkit_43150 | {"frames": 246, "switches": 0, "level_distribution": {"SAFE": 246}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 462 | 视频汇总[video_baseline] · results.mixkit_4331 | {"frames": 447, "switches": 2, "level_distribution": {"SAFE": 444, "WARNING": 3}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 463 | 视频汇总[video_baseline] · results.mixkit_4332 | {"frames": 596, "switches": 102, "level_distribution": {"WARNING": 362, "DANGER": 218, "SAFE": 16}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 464 | 视频汇总[video_baseline] · results.mixkit_4344 | {"frames": 388, "switches": 26, "level_distribution": {"SAFE": 372, "WARNING": 16}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 465 | 视频汇总[video_baseline] · results.mixkit_4348 | {"frames": 553, "switches": 10, "level_distribution": {"SAFE": 546, "WARNING": 7}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 466 | 视频汇总[video_baseline] · results.mixkit_4384 | {"frames": 185, "switches": 29, "level_distribution": {"WARNING": 15, "SAFE": 170}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 467 | 视频汇总[video_baseline] · results.mixkit_4390 | {"frames": 120, "switches": 66, "level_distribution": {"WARNING": 54, "SAFE": 43, "DANGER": 23}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 468 | 视频汇总[video_baseline] · results.mixkit_4401 | {"frames": 594, "switches": 218, "level_distribution": {"DANGER": 57, "WARNING": 383, "SAFE": 154}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 469 | 视频汇总[video_baseline] · results.mixkit_4437 | {"frames": 937, "switches": 260, "level_distribution": {"WARNING": 608, "DANGER": 227, "SAFE": 102}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 470 | 视频汇总[video_baseline] · results.mixkit_4450 | {"frames": 609, "switches": 0, "level_distribution": {"SAFE": 609}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 471 | 视频汇总[video_baseline] · results.mixkit_4451 | {"frames": 559, "switches": 54, "level_distribution": {"WARNING": 426, "SAFE": 91, "DANGER": 42}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 472 | 视频汇总[video_baseline] · results.mixkit_4519 | {"frames": 603, "switches": 0, "level_distribution": {"WARNING": 603}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 473 | 视频汇总[video_baseline] · results.mixkit_4530 | {"frames": 539, "switches": 0, "level_distribution": {"SAFE": 539}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 474 | 视频汇总[video_baseline] · results.mixkit_4547 | {"frames": 848, "switches": 92, "level_distribution": {"SAFE": 465, "WARNING": 383}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 475 | 视频汇总[video_baseline] · results.mixkit_4600 | {"frames": 284, "switches": 0, "level_distribution": {"SAFE": 284}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 476 | 视频汇总[video_baseline] · results.mixkit_4629 | {"frames": 177, "switches": 0, "level_distribution": {"SAFE": 177}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 477 | 视频汇总[video_baseline] · results.mixkit_4633 | {"frames": 552, "switches": 0, "level_distribution": {"SAFE": 552}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 478 | 视频汇总[video_baseline] · results.mixkit_4642 | {"frames": 460, "switches": 0, "level_distribution": {"SAFE": 460}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 479 | 视频汇总[video_baseline] · results.mixkit_4653 | {"frames": 151, "switches": 0, "level_distribution": {"SAFE": 151}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 480 | 视频汇总[video_baseline] · results.mixkit_4661 | {"frames": 230, "switches": 56, "level_distribution": {"SAFE": 71, "WARNING": 156, "DANGER": 3}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 481 | 视频汇总[video_baseline] · results.mixkit_46788 | {"frames": 344, "switches": 0, "level_distribution": {"SAFE": 344}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 482 | 视频汇总[video_baseline] · results.mixkit_47012 | {"frames": 456, "switches": 0, "level_distribution": {"SAFE": 456}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 483 | 视频汇总[video_baseline] · results.mixkit_47213 | {"frames": 1515, "switches": 0, "level_distribution": {"SAFE": 1515}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 484 | 视频汇总[video_baseline] · results.mixkit_4742 | {"frames": 299, "switches": 0, "level_distribution": {"SAFE": 299}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 485 | 视频汇总[video_baseline] · results.mixkit_47673 | {"frames": 435, "switches": 0, "level_distribution": {"SAFE": 435}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 486 | 视频汇总[video_baseline] · results.mixkit_47792 | {"frames": 425, "switches": 0, "level_distribution": {"SAFE": 425}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 487 | 视频汇总[video_baseline] · results.mixkit_4800 | {"frames": 439, "switches": 0, "level_distribution": {"SAFE": 439}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 488 | 视频汇总[video_baseline] · results.mixkit_4823 | {"frames": 309, "switches": 0, "level_distribution": {"SAFE": 309}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 489 | 视频汇总[video_baseline] · results.mixkit_4832 | {"frames": 249, "switches": 0, "level_distribution": {"SAFE": 249}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 490 | 视频汇总[video_baseline] · results.mixkit_4840 | {"frames": 368, "switches": 0, "level_distribution": {"SAFE": 368}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 491 | 视频汇总[video_baseline] · results.mixkit_48507 | {"frames": 646, "switches": 158, "level_distribution": {"SAFE": 492, "WARNING": 153, "DANGER": 1}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 492 | 视频汇总[video_baseline] · results.mixkit_48509 | {"frames": 333, "switches": 33, "level_distribution": {"SAFE": 311, "DANGER": 1, "WARNING": 21}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 493 | 视频汇总[video_baseline] · results.mixkit_48510 | {"frames": 274, "switches": 2, "level_distribution": {"SAFE": 273, "WARNING": 1}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 494 | 视频汇总[video_baseline] · results.mixkit_4855 | {"frames": 356, "switches": 12, "level_distribution": {"SAFE": 338, "WARNING": 18}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 495 | 视频汇总[video_baseline] · results.mixkit_4856 | {"frames": 412, "switches": 0, "level_distribution": {"SAFE": 412}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 496 | 视频汇总[video_baseline] · results.mixkit_4893 | {"frames": 161, "switches": 0, "level_distribution": {"SAFE": 161}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 497 | 视频汇总[video_baseline] · results.mixkit_49137 | {"frames": 595, "switches": 0, "level_distribution": {"SAFE": 595}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 498 | 视频汇总[video_baseline] · results.mixkit_4915 | {"frames": 360, "switches": 0, "level_distribution": {"SAFE": 360}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 499 | 视频汇总[video_baseline] · results.mixkit_4948 | {"frames": 247, "switches": 0, "level_distribution": {"SAFE": 247}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 500 | 视频汇总[video_baseline] · results.mixkit_49845 | {"frames": 501, "switches": 0, "level_distribution": {"SAFE": 501}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 501 | 视频汇总[video_baseline] · results.mixkit_49846 | {"frames": 1049, "switches": 0, "level_distribution": {"SAFE": 1049}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 502 | 视频汇总[video_baseline] · results.mixkit_49878 | {"frames": 343, "switches": 0, "level_distribution": {"SAFE": 343}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 503 | 视频汇总[video_baseline] · results.mixkit_50134 | {"frames": 388, "switches": 0, "level_distribution": {"SAFE": 388}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 504 | 视频汇总[video_baseline] · results.mixkit_513 | {"frames": 614, "switches": 0, "level_distribution": {"SAFE": 614}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 505 | 视频汇总[video_baseline] · results.mixkit_52427 | {"frames": 177, "switches": 0, "level_distribution": {"SAFE": 177}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 506 | 视频汇总[video_baseline] · results.mixkit_52454 | {"frames": 744, "switches": 0, "level_distribution": {"SAFE": 744}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 507 | 视频汇总[video_baseline] · results.mixkit_5396 | {"frames": 360, "switches": 0, "level_distribution": {"SAFE": 360}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 508 | 视频汇总[video_baseline] · results.mixkit_5944 | {"frames": 704, "switches": 0, "level_distribution": {"SAFE": 704}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 509 | 视频汇总[video_baseline] · results.mixkit_6302 | {"frames": 487, "switches": 0, "level_distribution": {"SAFE": 487}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 510 | 视频汇总[video_baseline] · results.mixkit_805 | {"frames": 404, "switches": 10, "level_distribution": {"SAFE": 363, "WARNING": 41}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 511 | 视频汇总[video_baseline] · results.mixkit_8739 | {"frames": 360, "switches": 0, "level_distribution": {"SAFE": 360}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 512 | 视频汇总[video_baseline] · results.mixkit_8869 | {"frames": 257, "switches": 14, "level_distribution": {"SAFE": 245, "WARNING": 12}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 513 | 视频汇总[video_baseline] · results.mixkit_9060 | {"frames": 175, "switches": 47, "level_distribution": {"WARNING": 66, "SAFE": 109}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 514 | 视频汇总[video_baseline] · results.mixkit_9607 | {"frames": 839, "switches": 0, "level_distribution": {"SAFE": 839}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 515 | 视频汇总[video_baseline] · results.mixkit_991 | {"frames": 594, "switches": 2, "level_distribution": {"SAFE": 593, "WARNING": 1}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 516 | 视频汇总[video_baseline] · results.mixkit_992 | {"frames": 281, "switches": 35, "level_distribution": {"WARNING": 44, "SAFE": 237}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 517 | 视频汇总[video_baseline] · results.mixkit_993 | {"frames": 975, "switches": 0, "level_distribution": {"SAFE": 975}} | `results/rerun/video_baseline/_summary.json` | video_baseline/_summary.json |
| 518 | 对比汇总 · n | 111 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 519 | 对比汇总 · base_frames | 45021 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 520 | 对比汇总 · enh_frames | 45021 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 521 | 对比汇总 · base_sw | 2578 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 522 | 对比汇总 · enh_sw | 650 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 523 | 对比汇总 · base_lv.SAFE | 37734 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 524 | 对比汇总 · base_lv.WARNING | 5097 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 525 | 对比汇总 · base_lv.DANGER | 2190 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 526 | 对比汇总 · enh_lv.SAFE | 26885 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 527 | 对比汇总 · enh_lv.WARNING | 6388 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 528 | 对比汇总 · enh_lv.DANGER | 11748 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 529 | 对比汇总 · base_danger | 24 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 530 | 对比汇总 · enh_danger | 66 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 531 | 对比汇总 · base_safe | 63 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 532 | 对比汇总 · enh_safe | 31 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 533 | 对比汇总 · pairs | list[111] | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 534 | 对比汇总 · _note | 基线已修正为 AND 规则（与真实引擎一致），旧 OR 基线数据不再使用。增强后 DANGER 帧/视频数上升是 FSM 评分更敏感（含密度权重）与 DANGER 保持机制所致，论文不得再表述为‘增强后 DANGER 下降’。 | `results/rerun/_comparison_summary.json` | _comparison_summary.json |
| 535 | 消融 · videos | list[6] | `results/rerun/_ablation.json` | _ablation.json |
| 536 | 消融 · combos | list[4] | `results/rerun/_ablation.json` | _ablation.json |
| 537 | 消融 · results.mixkit_134.A 基线(无增强) | {"frames": 600, "switches": 0, "dist": {"SAFE": 600}} | `results/rerun/_ablation.json` | _ablation.json |
| 538 | 消融 · results.mixkit_134.B +Kalman | {"frames": 600, "switches": 2, "dist": {"SAFE": 598, "WARNING": 2}} | `results/rerun/_ablation.json` | _ablation.json |
| 539 | 消融 · results.mixkit_134.C +FSM(无谱特征) | {"frames": 600, "switches": 12, "dist": {"SAFE": 293, "WARNING": 69, "DANGER": 238}} | `results/rerun/_ablation.json` | _ablation.json |
| 540 | 消融 · results.mixkit_134.D 完整(增强) | {"frames": 600, "switches": 12, "dist": {"SAFE": 306, "WARNING": 27, "DANGER": 267}} | `results/rerun/_ablation.json` | _ablation.json |
| 541 | 消融 · results.mixkit_13192.A 基线(无增强) | {"frames": 305, "switches": 48, "dist": {"SAFE": 90, "WARNING": 53, "DANGER": 162}} | `results/rerun/_ablation.json` | _ablation.json |
| 542 | 消融 · results.mixkit_13192.B +Kalman | {"frames": 305, "switches": 18, "dist": {"SAFE": 80, "WARNING": 46, "DANGER": 179}} | `results/rerun/_ablation.json` | _ablation.json |
| 543 | 消融 · results.mixkit_13192.C +FSM(无谱特征) | {"frames": 305, "switches": 4, "dist": {"SAFE": 6, "WARNING": 120, "DANGER": 179}} | `results/rerun/_ablation.json` | _ablation.json |
| 544 | 消融 · results.mixkit_13192.D 完整(增强) | {"frames": 305, "switches": 4, "dist": {"SAFE": 6, "WARNING": 120, "DANGER": 179}} | `results/rerun/_ablation.json` | _ablation.json |
| 545 | 消融 · results.mixkit_3428.A 基线(无增强) | {"frames": 456, "switches": 55, "dist": {"DANGER": 423, "WARNING": 32, "SAFE": 1}} | `results/rerun/_ablation.json` | _ablation.json |
| 546 | 消融 · results.mixkit_3428.B +Kalman | {"frames": 456, "switches": 0, "dist": {"DANGER": 456}} | `results/rerun/_ablation.json` | _ablation.json |
| 547 | 消融 · results.mixkit_3428.C +FSM(无谱特征) | {"frames": 456, "switches": 2, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 451}} | `results/rerun/_ablation.json` | _ablation.json |
| 548 | 消融 · results.mixkit_3428.D 完整(增强) | {"frames": 456, "switches": 2, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 451}} | `results/rerun/_ablation.json` | _ablation.json |
| 549 | 消融 · results.mixkit_4519.A 基线(无增强) | {"frames": 603, "switches": 0, "dist": {"WARNING": 603}} | `results/rerun/_ablation.json` | _ablation.json |
| 550 | 消融 · results.mixkit_4519.B +Kalman | {"frames": 603, "switches": 0, "dist": {"WARNING": 603}} | `results/rerun/_ablation.json` | _ablation.json |
| 551 | 消融 · results.mixkit_4519.C +FSM(无谱特征) | {"frames": 603, "switches": 2, "dist": {"SAFE": 2, "WARNING": 81, "DANGER": 520}} | `results/rerun/_ablation.json` | _ablation.json |
| 552 | 消融 · results.mixkit_4519.D 完整(增强) | {"frames": 603, "switches": 2, "dist": {"SAFE": 2, "WARNING": 3, "DANGER": 598}} | `results/rerun/_ablation.json` | _ablation.json |
| 553 | 消融 · results.mixkit_4178.A 基线(无增强) | {"frames": 298, "switches": 96, "dist": {"SAFE": 213, "WARNING": 83, "DANGER": 2}} | `results/rerun/_ablation.json` | _ablation.json |
| 554 | 消融 · results.mixkit_4178.B +Kalman | {"frames": 298, "switches": 30, "dist": {"SAFE": 53, "WARNING": 193, "DANGER": 52}} | `results/rerun/_ablation.json` | _ablation.json |
| 555 | 消融 · results.mixkit_4178.C +FSM(无谱特征) | {"frames": 298, "switches": 6, "dist": {"SAFE": 13, "WARNING": 176, "DANGER": 109}} | `results/rerun/_ablation.json` | _ablation.json |
| 556 | 消融 · results.mixkit_4178.D 完整(增强) | {"frames": 298, "switches": 6, "dist": {"SAFE": 3, "WARNING": 141, "DANGER": 154}} | `results/rerun/_ablation.json` | _ablation.json |
| 557 | 消融 · results.mixkit_50134.A 基线(无增强) | {"frames": 388, "switches": 0, "dist": {"SAFE": 388}} | `results/rerun/_ablation.json` | _ablation.json |
| 558 | 消融 · results.mixkit_50134.B +Kalman | {"frames": 388, "switches": 0, "dist": {"SAFE": 388}} | `results/rerun/_ablation.json` | _ablation.json |
| 559 | 消融 · results.mixkit_50134.C +FSM(无谱特征) | {"frames": 388, "switches": 0, "dist": {"SAFE": 388}} | `results/rerun/_ablation.json` | _ablation.json |
| 560 | 消融 · results.mixkit_50134.D 完整(增强) | {"frames": 388, "switches": 0, "dist": {"SAFE": 388}} | `results/rerun/_ablation.json` | _ablation.json |
| 561 | 谱特征FSM实验 · method | 离线重算：results/rerun/video_enhanced/*/timeline.json；M0=原配置（校验），M1=谱正反馈，M2=谱抑制 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 562 | 谱特征FSM实验 · fiedler_norm | fiedler/(fiedler+1)；fiedler<=0 视为不可用（中性 0.5） | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 563 | 谱特征FSM实验 · now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 564 | 谱特征FSM实验 · scenarios | list[10] | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 565 | 谱特征FSM实验 · video_summary.M0.mode | M0 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 566 | 谱特征FSM实验 · video_summary.M0.w4 | 0.0 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 567 | 谱特征FSM实验 · video_summary.M0.frames | 45021 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 568 | 谱特征FSM实验 · video_summary.M0.DANGER_frames | 7906 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 569 | 谱特征FSM实验 · video_summary.M0.WARNING_frames | 8901 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 570 | 谱特征FSM实验 · video_summary.M0.SAFE_frames | 28214 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 571 | 谱特征FSM实验 · video_summary.M0.switches_total | 546 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 572 | 谱特征FSM实验 · video_summary.M0.switches_avg_per_video | 4.92 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 573 | 谱特征FSM实验 · video_summary.M0.DANGER_videos | 40 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 574 | 谱特征FSM实验 · video_summary.M0.now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 575 | 谱特征FSM实验 · video_summary.M1_0.05.mode | M1 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 576 | 谱特征FSM实验 · video_summary.M1_0.05.w4 | 0.05 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 577 | 谱特征FSM实验 · video_summary.M1_0.05.frames | 45021 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 578 | 谱特征FSM实验 · video_summary.M1_0.05.DANGER_frames | 11918 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 579 | 谱特征FSM实验 · video_summary.M1_0.05.WARNING_frames | 6977 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 580 | 谱特征FSM实验 · video_summary.M1_0.05.SAFE_frames | 26126 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 581 | 谱特征FSM实验 · video_summary.M1_0.05.switches_total | 603 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 582 | 谱特征FSM实验 · video_summary.M1_0.05.switches_avg_per_video | 5.43 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 583 | 谱特征FSM实验 · video_summary.M1_0.05.DANGER_videos | 65 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 584 | 谱特征FSM实验 · video_summary.M1_0.05.now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 585 | 谱特征FSM实验 · video_summary.M1_0.1.mode | M1 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 586 | 谱特征FSM实验 · video_summary.M1_0.1.w4 | 0.1 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 587 | 谱特征FSM实验 · video_summary.M1_0.1.frames | 45021 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 588 | 谱特征FSM实验 · video_summary.M1_0.1.DANGER_frames | 12659 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 589 | 谱特征FSM实验 · video_summary.M1_0.1.WARNING_frames | 7604 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 590 | 谱特征FSM实验 · video_summary.M1_0.1.SAFE_frames | 24758 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 591 | 谱特征FSM实验 · video_summary.M1_0.1.switches_total | 650 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 592 | 谱特征FSM实验 · video_summary.M1_0.1.switches_avg_per_video | 5.86 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 593 | 谱特征FSM实验 · video_summary.M1_0.1.DANGER_videos | 68 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 594 | 谱特征FSM实验 · video_summary.M1_0.1.now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 595 | 谱特征FSM实验 · video_summary.M1_0.2.mode | M1 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 596 | 谱特征FSM实验 · video_summary.M1_0.2.w4 | 0.2 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 597 | 谱特征FSM实验 · video_summary.M1_0.2.frames | 45021 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 598 | 谱特征FSM实验 · video_summary.M1_0.2.DANGER_frames | 14435 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 599 | 谱特征FSM实验 · video_summary.M1_0.2.WARNING_frames | 7310 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 600 | 谱特征FSM实验 · video_summary.M1_0.2.SAFE_frames | 23276 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 601 | 谱特征FSM实验 · video_summary.M1_0.2.switches_total | 610 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 602 | 谱特征FSM实验 · video_summary.M1_0.2.switches_avg_per_video | 5.5 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 603 | 谱特征FSM实验 · video_summary.M1_0.2.DANGER_videos | 70 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 604 | 谱特征FSM实验 · video_summary.M1_0.2.now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 605 | 谱特征FSM实验 · video_summary.M2_0.05.mode | M2 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 606 | 谱特征FSM实验 · video_summary.M2_0.05.w4 | 0.05 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 607 | 谱特征FSM实验 · video_summary.M2_0.05.frames | 45021 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 608 | 谱特征FSM实验 · video_summary.M2_0.05.DANGER_frames | 4954 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 609 | 谱特征FSM实验 · video_summary.M2_0.05.WARNING_frames | 11000 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 610 | 谱特征FSM实验 · video_summary.M2_0.05.SAFE_frames | 29067 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 611 | 谱特征FSM实验 · video_summary.M2_0.05.switches_total | 473 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 612 | 谱特征FSM实验 · video_summary.M2_0.05.switches_avg_per_video | 4.26 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 613 | 谱特征FSM实验 · video_summary.M2_0.05.DANGER_videos | 26 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 614 | 谱特征FSM实验 · video_summary.M2_0.05.now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 615 | 谱特征FSM实验 · video_summary.M2_0.1.mode | M2 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 616 | 谱特征FSM实验 · video_summary.M2_0.1.w4 | 0.1 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 617 | 谱特征FSM实验 · video_summary.M2_0.1.frames | 45021 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 618 | 谱特征FSM实验 · video_summary.M2_0.1.DANGER_frames | 3580 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 619 | 谱特征FSM实验 · video_summary.M2_0.1.WARNING_frames | 12047 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 620 | 谱特征FSM实验 · video_summary.M2_0.1.SAFE_frames | 29394 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 621 | 谱特征FSM实验 · video_summary.M2_0.1.switches_total | 444 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 622 | 谱特征FSM实验 · video_summary.M2_0.1.switches_avg_per_video | 4.0 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 623 | 谱特征FSM实验 · video_summary.M2_0.1.DANGER_videos | 21 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 624 | 谱特征FSM实验 · video_summary.M2_0.1.now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 625 | 谱特征FSM实验 · video_summary.M2_0.2.mode | M2 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 626 | 谱特征FSM实验 · video_summary.M2_0.2.w4 | 0.2 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 627 | 谱特征FSM实验 · video_summary.M2_0.2.frames | 45021 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 628 | 谱特征FSM实验 · video_summary.M2_0.2.DANGER_frames | 1869 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 629 | 谱特征FSM实验 · video_summary.M2_0.2.WARNING_frames | 13072 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 630 | 谱特征FSM实验 · video_summary.M2_0.2.SAFE_frames | 30080 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 631 | 谱特征FSM实验 · video_summary.M2_0.2.switches_total | 408 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 632 | 谱特征FSM实验 · video_summary.M2_0.2.switches_avg_per_video | 3.68 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 633 | 谱特征FSM实验 · video_summary.M2_0.2.DANGER_videos | 12 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 634 | 谱特征FSM实验 · video_summary.M2_0.2.now_ms_mode | ms33 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 635 | 谱特征FSM实验 · m2_vs_m0_analysis.diff.D降级为非D | 4360 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 636 | 谱特征FSM实验 · m2_vs_m0_analysis.diff.非D升级为D | 34 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 637 | 谱特征FSM实验 · m2_vs_m0_analysis.diff.W降级为S | 991 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 638 | 谱特征FSM实验 · m2_vs_m0_analysis.diff.S升级为W/D | 6 | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 639 | 谱特征FSM实验 · m2_vs_m0_analysis.fiedler_stats.M0_DANGER帧 | {"N": 7906, "mean": 1.1008, "median": 0.7123} | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 640 | 谱特征FSM实验 · m2_vs_m0_analysis.fiedler_stats.M2仍DANGER帧 | {"N": 3546, "mean": 1.11, "median": 0.6901} | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 641 | 谱特征FSM实验 · m2_vs_m0_analysis.fiedler_stats.M2被降级(原D)帧 | {"N": 4360, "mean": 1.0933, "median": 0.817} | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 642 | 谱特征FSM实验 · m2_vs_m0_analysis.video_diff | list[20] | `results/rerun/_spectral_fsm_experiment.json` | _spectral_fsm_experiment.json |
| 643 | 场景测试 · edge_dist_m | 2.0 | `results/scenarios/scenario_test.json` | scenario_test.json |
| 644 | 场景测试 · method | 合成场景 -> 图网络特征 -> 规则分级 + RiskFSM（连续20帧） | `results/scenarios/scenario_test.json` | scenario_test.json |
| 645 | 场景测试 · scenarios | list[10] | `results/scenarios/scenario_test.json` | scenario_test.json |
| 646 | 场景测试 · conclusions | list[4] | `results/scenarios/scenario_test.json` | scenario_test.json |
| 647 | 实验总摘要 · _note | 2026-10-28 由 results/rerun/video_enhanced/（谱特征接入后）全量时间线重新生成。 | `results/experiment_summary.json` | experiment_summary.json |
| 648 | 实验总摘要 · timelines.mixkit_13192.frames | 305 | `results/experiment_summary.json` | experiment_summary.json |
| 649 | 实验总摘要 · timelines.mixkit_13192.dist | {"SAFE": 6, "WARNING": 120, "DANGER": 179} | `results/experiment_summary.json` | experiment_summary.json |
| 650 | 实验总摘要 · timelines.mixkit_13192.switches | 4 | `results/experiment_summary.json` | experiment_summary.json |
| 651 | 实验总摘要 · timelines.mixkit_13192.avg_n | 15.4 | `results/experiment_summary.json` | experiment_summary.json |
| 652 | 实验总摘要 · timelines.mixkit_13265.frames | 281 | `results/experiment_summary.json` | experiment_summary.json |
| 653 | 实验总摘要 · timelines.mixkit_13265.dist | {"SAFE": 281, "WARNING": 0, "DANGER": 0} | `results/experiment_summary.json` | experiment_summary.json |
| 654 | 实验总摘要 · timelines.mixkit_13265.switches | 0 | `results/experiment_summary.json` | experiment_summary.json |
| 655 | 实验总摘要 · timelines.mixkit_13265.avg_n | 0.2 | `results/experiment_summary.json` | experiment_summary.json |
| 656 | 实验总摘要 · timelines.mixkit_134.frames | 600 | `results/experiment_summary.json` | experiment_summary.json |
| 657 | 实验总摘要 · timelines.mixkit_134.dist | {"SAFE": 306, "WARNING": 27, "DANGER": 267} | `results/experiment_summary.json` | experiment_summary.json |
| 658 | 实验总摘要 · timelines.mixkit_134.switches | 12 | `results/experiment_summary.json` | experiment_summary.json |
| 659 | 实验总摘要 · timelines.mixkit_134.avg_n | 2.1 | `results/experiment_summary.json` | experiment_summary.json |
| 660 | 实验总摘要 · timelines.mixkit_14084.frames | 255 | `results/experiment_summary.json` | experiment_summary.json |
| 661 | 实验总摘要 · timelines.mixkit_14084.dist | {"SAFE": 67, "WARNING": 159, "DANGER": 29} | `results/experiment_summary.json` | experiment_summary.json |
| 662 | 实验总摘要 · timelines.mixkit_14084.switches | 13 | `results/experiment_summary.json` | experiment_summary.json |
| 663 | 实验总摘要 · timelines.mixkit_14084.avg_n | 6.5 | `results/experiment_summary.json` | experiment_summary.json |
| 664 | 实验总摘要 · timelines.mixkit_151.frames | 336 | `results/experiment_summary.json` | experiment_summary.json |
| 665 | 实验总摘要 · timelines.mixkit_151.dist | {"SAFE": 300, "WARNING": 8, "DANGER": 28} | `results/experiment_summary.json` | experiment_summary.json |
| 666 | 实验总摘要 · timelines.mixkit_151.switches | 4 | `results/experiment_summary.json` | experiment_summary.json |
| 667 | 实验总摘要 · timelines.mixkit_151.avg_n | 1.4 | `results/experiment_summary.json` | experiment_summary.json |
| 668 | 实验总摘要 · image_baseline.total | 500 | `results/experiment_summary.json` | experiment_summary.json |
| 669 | 实验总摘要 · image_baseline.processed | 500 | `results/experiment_summary.json` | experiment_summary.json |
| 670 | 实验总摘要 · image_baseline.failed | 0 | `results/experiment_summary.json` | experiment_summary.json |
| 671 | 实验总摘要 · image_baseline.levels.SAFE | 414 | `results/experiment_summary.json` | experiment_summary.json |
| 672 | 实验总摘要 · image_baseline.levels.WARNING | 61 | `results/experiment_summary.json` | experiment_summary.json |
| 673 | 实验总摘要 · image_baseline.levels.DANGER | 25 | `results/experiment_summary.json` | experiment_summary.json |
| 674 | 实验总摘要 · image_baseline.gt_total | 3089 | `results/experiment_summary.json` | experiment_summary.json |
| 675 | 实验总摘要 · image_baseline.det_total | 2548 | `results/experiment_summary.json` | experiment_summary.json |
| 676 | 实验总摘要 · image_baseline.det_rate | 0.8249 | `results/experiment_summary.json` | experiment_summary.json |
| 677 | 实验总摘要 · image_baseline.high_density_det_rate | 0.7943 | `results/experiment_summary.json` | experiment_summary.json |
| 678 | 实验总摘要 · image_baseline.high_density_images | 12 | `results/experiment_summary.json` | experiment_summary.json |
| 679 | 实验总摘要 · image_baseline.elapsed_s | None | `results/experiment_summary.json` | experiment_summary.json |
| 680 | 真机配置 · edge_dist_m | 2.0 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 681 | 真机配置 · frame_dt | 0.1 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 682 | 真机配置 · fsm.w1 | 0.4 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 683 | 真机配置 · fsm.w2 | 0.3 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 684 | 真机配置 · fsm.w3 | 0.3 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 685 | 真机配置 · fsm.w4 | 0.1 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 686 | 真机配置 · fsm.n_ref | 20 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 687 | 真机配置 · fsm.up_warning | 0.4 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 688 | 真机配置 · fsm.up_danger | 0.65 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 689 | 真机配置 · fsm.down_safe | 0.28 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 690 | 真机配置 · fsm.down_warning | 0.55 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 691 | 真机配置 · fsm.confirm_up | 3 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 692 | 真机配置 · fsm.confirm_down | 5 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 693 | 真机配置 · fsm.danger_hold_frames | 10 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 694 | 真机配置 · fsm.danger_hold_ms | 1000 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 695 | 真机配置 · fsm.trend_bias | 0.05 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 696 | 真机配置 · fsm.min_danger_n | 3 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 697 | 真机配置 · enable_risk_engine_fallback | False | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 698 | 真机配置 · homography_file |  | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 699 | 真机配置 · target_classes | list[1] | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 700 | 真机配置 · danger.n | 10 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 701 | 真机配置 · danger.rho | 0.55 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 702 | 真机配置 · danger.gcc | 0.8 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 703 | 真机配置 · warning.n | 5 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 704 | 真机配置 · warning.rho | 0.28 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 705 | 真机配置 · warning.gcc | 0.55 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 706 | 真机配置 · _comment | 规则阈值配置。target_classes=检测目标类别集合（COCO id，默认[0]=person；车辆[2,5,7,3]、狗[16]、猫[15]、动物[14,18,19,20,21]）。class_conf_thresholds=按类别差异化置信度阈值（COCO id -> 最低置信度0~1，可选；空对象=不区分，仅用全局检测阈值）。homography_file=外部IPM单应标定文件（文本3x3，与 ipm_calibrate.py --save xxx.txt 输出兼容；空串=用默认硬编码单应，相机位姿变化时配置此路径即可无需重编译）。danger/warning 的 n=人数阈值 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 707 | 真机配置 · conf_threshold | 0.6 | `results/edge_board/default_config.json` | edge_board/default_config.json |
| 708 | 真机报告 · FPS（原文出现值） | 6.9 | `results/edge_board/EDGE_TEST_REPORT.md` | 正则 (\d+(?:\.\d+)?)\s*FPS |
| 709 | 真机报告 · MB 内存（原文出现值） | 256 / 12.5 / 211 / 157 | `results/edge_board/EDGE_TEST_REPORT.md` | 正则 (\d+(?:\.\d+)?)\s*MB |
| 710 | 真机长稳 · 采样记录条数 | 10 | `results/edge_board/long_stability.jsonl` | 非空行计数 |
| 711 | 代码 · src/algorithms 文件数 | 34 | `src/algorithms` | 目录递归计数 |
| 712 | 代码 · src/hardware 文件数 | 15 | `src/hardware` | 目录递归计数 |
| 713 | 代码 · src/luckfox_rtsp_yolov5 文件数 | 51 | `src/luckfox_rtsp_yolov5` | 目录递归计数 |
| 714 | 代码 · C/C++ 源文件数（src/） | 57 | `src` | 后缀统计 |
| 715 | 代码 · Python 文件数（src/） | 9 | `src` | 后缀统计 |
| 716 | 代码 · tests/ 文件数 | 120 | `tests` | 目录递归计数 |
| 717 | 代码 · scripts/ 文件数 | 55 | `scripts` | 目录递归计数 |
| 718 | 代码 · deploy/ 文件数 | 13 | `deploy` | 目录递归计数 |
| 719 | 代码 · tools/ 文件数 | 7 | `tools` | 目录递归计数 |
| 720 | 代码 · config/ 文件数 | 1 | `config` | 目录递归计数 |
| 721 | 代码 · dataset/ 文件数 | 666 | `dataset` | 目录递归计数 |
| 722 | 代码 · pytest 用例数（tests/*.py 中 def test_） | 131 | `tests` | 正则计数 |
| 723 | 硬件 · docs/hardware 文件数 | 19 | `docs/hardware` | 目录计数 |
| 724 | 硬件 · 文档 DISPLAY_MENU_SYSTEM_PLAN.md | 存在 | `docs/hardware/DISPLAY_MENU_SYSTEM_PLAN.md` | 文件存在 |
| 725 | 硬件 · 文档 HARDWARE_BUILD_GUIDE.md | 存在 | `docs/hardware/HARDWARE_BUILD_GUIDE.md` | 文件存在 |
| 726 | 硬件 · 文档 HARDWARE_EXPANSION_PLAN.md | 存在 | `docs/hardware/HARDWARE_EXPANSION_PLAN.md` | 文件存在 |
| 727 | 硬件 · 文档 PCB_BOARD_DESIGN.md | 存在 | `docs/hardware/PCB_BOARD_DESIGN.md` | 文件存在 |
| 728 | 硬件 · 文档 led_button_driver.md | 存在 | `docs/hardware/led_button_driver.md` | 文件存在 |
| 729 | 硬件 · 文档 lvgl_board_port.md | 存在 | `docs/hardware/lvgl_board_port.md` | 文件存在 |
| 730 | 硬件 · 文档 lvgl_menu.md | 存在 | `docs/hardware/lvgl_menu.md` | 文件存在 |
| 731 | 硬件 · 文档 oled_switch.md | 存在 | `docs/hardware/oled_switch.md` | 文件存在 |
| 732 | 硬件 · 文档 pin_map_verified.md | 存在 | `docs/hardware/pin_map_verified.md` | 文件存在 |
| 733 | 硬件 · 文档 roi_draw_touch.md | 存在 | `docs/hardware/roi_draw_touch.md` | 文件存在 |
| 734 | 硬件 · 文档 selftest.md | 存在 | `docs/hardware/selftest.md` | 文件存在 |
| 735 | 硬件 · 文档 system_integration.md | 存在 | `docs/hardware/system_integration.md` | 文件存在 |
| 736 | 硬件 · 文档 touchscreen.md | 存在 | `docs/hardware/touchscreen.md` | 文件存在 |
| 737 | 硬件 · 文档 tracking_servo.md | 存在 | `docs/hardware/tracking_servo.md` | 文件存在 |
| 738 | 硬件 · 文档 voice_module.md | 存在 | `docs/hardware/voice_module.md` | 文件存在 |
