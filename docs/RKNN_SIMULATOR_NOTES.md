# RKNN 模拟器仿真可行性验证（2026-10-27）

> 背景：P2 评估（320×320 模型压缩能否降低 NPU 91ms 瓶颈）前，验证 rknn-toolkit2
> 在 PC 上能否用模拟器仿真评估性能。环境：老电脑（192.168.0.68，Ubuntu 22.04 x86_64，
> Python 3.10 venv `/home/liao/rknn_venv`，rknn-toolkit2 2.5.0，wheel 来自
> `airockchip/rknn-toolkit2` 仓库 `rknn-toolkit2/packages/x86_64/`）。

## 一、直接回答

1. **`init_runtime(target='rv1106')` 不是模拟器模式**。它表示连接真实 RV1106 设备，
   需要 adb 能找到设备（报错：`Get board target failed ... no devices/emulators found`）。
   纯 PC 模拟器仿真的正确写法是 **`init_runtime()`（不传 target）**。

2. **`eval_perf` 签名**：`(self, is_print=True, fix_freq=True)`，返回性能结果 dict。
   但 **`eval_perf` 在模拟器环境不支持**（报错：`Not support in simulator environment,
   please set target in init_runtime!`），只能在指定真机 target 时调用。

## 二、模拟器模式的限制（实测）

| 项 | 行为 |
|---|---|
| `load_rknn` 加载已构建 `.rknn` 后 `init_runtime()` | 报错：`not support inference on the simulator, please set target first`（已构建模型不能在模拟器推理） |
| 模拟器推理正确流程 | `config(target_platform='rv1106')` → `load_onnx` → `build(do_quantization=True, dataset=...)` → `init_runtime()` |
| RV1106 量化要求 | `build(do_quantization=False)` 报错：`Current target_platform(rv1106) not support do_quantization = False`（必须量化） |
| 输入格式 | 模拟器要求 **NHWC**（如 `(1,640,640,3)`）；NCHW `(1,3,640,640)` 报错 |
| 模拟器推理 | 成功（yolov5n.onnx 量化后输出 3 个张量 `(1,255,80,80)/(40,40)/(20,20)`） |
| `eval_perf` | 模拟器下不可用，必须连真机 |

## 三、对 P2（320×320 模型压缩）评估的含义

- **PC 模拟器无法给出可信性能数据**（eval_perf 不支持）。性能评估只有两条路：
  1. 真机：开发板通过 **USB/adb 连接到老电脑**，`init_runtime(target='rv1106', device_id=...)`
     后跑 `eval_perf`（可固定频率，`fix_freq=True`）；
  2. 沿用现有板端 `[PROFILE]` 打点（更贴近实际，已有 cvt_resize/letterbox/rknn 分段耗时）。
- 模拟器仍可用于**功能/精度验证**：量化构建后模拟推理，比对输出与参考（如 coco 标签），
  但速度不代表真机。
- 若评估 320×320 输入：可先在 PC 用模拟器验证量化模型输出正确性，再部署板端用
  `[PROFILE]` 实测 rknn 耗时是否下降。

## 四、环境留存

- 老电脑：`/home/liao/rknn_venv`（rknn-toolkit2 2.5.0 + torch 2.4.0 CPU + onnx/onnxruntime/opencv）
- 测试脚本：`/home/liao/rknn_sim_test.py`（eval_perf 签名 + target='rv1106' 失败路径）、
  `rknn_sim_test4.py`（onnx→量化 build→模拟器推理成功路径）
- 模型：`/home/liao/yolov5n_external.rknn`（板端现用模型副本）、
  `/home/liao/luckfox/rknn_model_zoo/examples/yolov5/model/yolov5n.onnx`
- 校准集：`/home/liao/dataset_yolov5n.txt`（指向 bus.jpg）