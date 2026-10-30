# =============================================================================
# Makefile — 边缘端行人风险决策 C++ 工程构建脚本
# -----------------------------------------------------------------------------
# 两个用途：
#   1) `make test`   在主机（x86）编译并运行算法单元测试，无需 OpenCV/RKNN/RTSP。
#   2) `make luckfox` 交叉编译开发板完整工程（需 Luckfox SDK 环境变量，见下方）。
#   3) `make bench`  帧率微基准（主机 x86）：量化谱特征降频与预处理合并的收益。
#
# 依赖：
#   - 主机测试仅需 g++（C++11）。
#   - 交叉编译需要 Luckfox SDK（board_sdk / luckfox_sdk，含 arm 工具链 + rknn 头
#     + 库）。注意：yolov5.h / postprocess.h 由 SDK 的 include/rknn 提供，仓库内
#     没有这两个文件，所以 -I 必须包含 $(LUCKFOX_SDK_ROOT)/include/rknn。
# =============================================================================

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra
INC       = -Iinclude -I../algorithms

SRC_DIR = src
TEST_DIR = tests

# ---- 算法 header-only 头文件（测试依赖）----
ALGO_HEADERS = include/enhanced_decision.h include/risk_config.h \
               ../algorithms/graph_laplacian.h ../algorithms/kalman_filter.h \
               ../algorithms/risk_fsm.h ../algorithms/minijson.h

.PHONY: all test bench test-enhanced test-minijson test-risk-config test-golden \
        test-preprocess test-selftest test-lcd-menu test-ili9341 test-xpt2046 \
        test-sysfs-spi clean help luckfox

all: test

# ---------------------------------------------------------------------------
# 主机单元测试
# ---------------------------------------------------------------------------
# Linux 板端适配层测试（SysfsSpiBus 依赖 <linux/spi/spidev.h>，仅 Linux 编译；
# Windows 主机/交叉编译环境自动排除，CI（ubuntu）会自动纳入）。
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
LINUX_TESTS := test-sysfs-spi
else
LINUX_TESTS :=
endif

test: test-enhanced test-minijson test-risk-config test-golden test-preprocess test-hardware test-selftest test-lcd-menu test-ili9341 test-xpt2046 $(LINUX_TESTS)

test-enhanced: $(TEST_DIR)/test_enhanced_decision.cc $(SRC_DIR)/enhanced_decision.cc $(ALGO_HEADERS)
	$(CXX) $(CXXFLAGS) $(INC) $(TEST_DIR)/test_enhanced_decision.cc $(SRC_DIR)/enhanced_decision.cc \
	    -o $(TEST_DIR)/test_enhanced_decision
	./$(TEST_DIR)/test_enhanced_decision

test-minijson: $(TEST_DIR)/test_minijson.cc ../algorithms/minijson.h
	$(CXX) $(CXXFLAGS) -I../algorithms $(TEST_DIR)/test_minijson.cc -o $(TEST_DIR)/test_minijson
	./$(TEST_DIR)/test_minijson

# 若主机装有 OpenCV（pkg-config 可找到），则一并编译 risk_engine.cc 并验证
# RiskEngine::setConfig 生效；否则只验证 risk_config 的 JSON 读取逻辑。
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4 2>/dev/null)
ifneq ($(strip $(OPENCV_CFLAGS)),)
RISK_CONFIG_SRC := $(SRC_DIR)/risk_engine.cc
RISK_CONFIG_LIBS := $(shell pkg-config --libs opencv4)
endif

test-risk-config: $(TEST_DIR)/test_risk_config.cc $(SRC_DIR)/risk_engine.cc ../algorithms/minijson.h include/risk_config.h
	$(CXX) $(CXXFLAGS) $(INC) $(TEST_DIR)/test_risk_config.cc $(RISK_CONFIG_SRC) $(RISK_CONFIG_LIBS) \
	    -o $(TEST_DIR)/test_risk_config
	./$(TEST_DIR)/test_risk_config

# 黄金回归：C++ 实现读取 tests/golden/*.json（由 scripts/generate_golden.py
# 用 Python 参考实现生成），与期望输出比对，防止 C++/Python 双实现漂移。
test-golden: $(TEST_DIR)/test_golden_regression.cc ../algorithms/graph_laplacian.h \
             ../algorithms/ema_smoother.h ../algorithms/risk_fsm.h ../algorithms/minijson.h
	$(CXX) $(CXXFLAGS) $(INC) $(TEST_DIR)/test_golden_regression.cc \
	    -o $(TEST_DIR)/test_golden_regression
	./$(TEST_DIR)/test_golden_regression ../../tests/golden

# 图像预处理黄金回归：tests/golden/preprocess/*（由
# scripts/generate_preprocess_golden.py 用 OpenCV 参考生成）。验证
# preprocess.{h,cc} 的 NV12->BGR 与 NV12->RGB letterbox 与 OpenCV 流程一致。
test-preprocess: $(TEST_DIR)/test_preprocess.cc $(SRC_DIR)/preprocess.cc \
                 include/preprocess.h ../algorithms/minijson.h
	$(CXX) $(CXXFLAGS) $(INC) $(TEST_DIR)/test_preprocess.cc $(SRC_DIR)/preprocess.cc \
	    -o $(TEST_DIR)/test_preprocess
	./$(TEST_DIR)/test_preprocess ../../tests/golden/preprocess

# 硬件驱动模块（三色 LED + 按键 + 舵机云台 + 语音告警）主机测试：使用
# FakeGpio/FakePwm 内存替身，验证颜色映射/防抖/边沿检测/角度换算/云台
# 跟踪逻辑，无 Linux/GPIO 依赖。
test-hardware: $(TEST_DIR)/test_hardware_modules.cc ../hardware/gpio.h \
               ../hardware/led.h ../hardware/button.h ../hardware/servo.h \
               ../hardware/pan_tilt.h ../hardware/voice_alert.h \
               ../hardware/voice_alert.cc
	$(CXX) $(CXXFLAGS) -I../hardware $(TEST_DIR)/test_hardware_modules.cc \
	    ../hardware/voice_alert.cc \
	    -o $(TEST_DIR)/test_hardware_modules
	./$(TEST_DIR)/test_hardware_modules

# 硬件自检框架（GPIO 写读回/PWM 节点/SPI 屏幕 ID/USB 声卡/WiFi/按键）
# 主机测试：使用 FakeGpio/FakePwm/FakeSelftestProbe 内存替身，验证
# PASS/FAIL 判定、统计接口与 NULL 容错，无 Linux/GPIO 依赖。
test-selftest: $(TEST_DIR)/test_selftest.cc ../hardware/selftest.h \
               ../hardware/gpio.h ../hardware/servo.h
	$(CXX) $(CXXFLAGS) -I../hardware $(TEST_DIR)/test_selftest.cc \
	    -o $(TEST_DIR)/test_selftest
	./$(TEST_DIR)/test_selftest

# 触摸屏菜单状态机框架（LVGL 前端无关逻辑）：使用 FakeMenuBackend 内存
# 替身，验证页面导航/四键处理/风险等级颜色映射/参数刷新节流/NULL 容错，
# 无 LVGL/ILI9341 硬件依赖。
test-lcd-menu: $(TEST_DIR)/test_lcd_menu.cc ../hardware/lcd_menu.h
	$(CXX) $(CXXFLAGS) -I../hardware $(TEST_DIR)/test_lcd_menu.cc \
	    -o $(TEST_DIR)/test_lcd_menu
	./$(TEST_DIR)/test_lcd_menu

# ILI9341 SPI 显示驱动（320x240 RGB565）：使用 FakeSpi/FakeLcdPins 内存替身，
# 验证复位/初始化命令序列/窗口参数/矩形填充裁剪/帧缓冲/整帧 flush/读 ID，
# 无 ILI9341 硬件依赖。
test-ili9341: $(TEST_DIR)/test_ili9341.cc ../hardware/ili9341.h
	$(CXX) $(CXXFLAGS) -I../hardware $(TEST_DIR)/test_ili9341.cc \
	    -o $(TEST_DIR)/test_ili9341
	./$(TEST_DIR)/test_ili9341

# XPT2046 电阻触摸控制器（12 位 ADC 坐标/压力）：使用 FakeSpi/FakeTouchPin
# 内存替身，验证命令字节/数据提取/触摸判定（PENIRQ+压力阈值）/校准坐标转换/
# 中值抗噪/故障注入，无 XPT2046 硬件依赖。
test-xpt2046: $(TEST_DIR)/test_xpt2046.cc ../hardware/xpt2046.h \
               ../hardware/ili9341.h
	$(CXX) $(CXXFLAGS) -I../hardware $(TEST_DIR)/test_xpt2046.cc \
	    -o $(TEST_DIR)/test_xpt2046
	./$(TEST_DIR)/test_xpt2046

# Linux 板端适配层（spidev 总线 + sysfs GPIO 引脚）：FakeGpio 注入验证
# DC/RESET/背光/PENIRQ 逻辑与 spidev 失败路径。依赖 Linux 头文件，仅
# Linux 环境编译运行（CI ubuntu 自动纳入；Windows 主机被 test 排除）。
test-sysfs-spi: $(TEST_DIR)/test_sysfs_spi.cc ../hardware/sysfs_spi.h \
                 ../hardware/sysfs_spi.cc ../hardware/gpio.h \
                 ../hardware/ili9341.h ../hardware/xpt2046.h
	$(CXX) $(CXXFLAGS) -I../hardware $(TEST_DIR)/test_sysfs_spi.cc \
	    ../hardware/sysfs_spi.cc \
	    -o $(TEST_DIR)/test_sysfs_spi
	./$(TEST_DIR)/test_sysfs_spi

# 板端硬件自检 CLI（真实 Sysfs 检测，退出码=失败数，供 systemd 门禁）
hardware-selftest: ../hardware/selftest_main.cc ../hardware/selftest.h \
                   ../hardware/gpio.h ../hardware/servo.h
	$(CXX) $(CXXFLAGS) -I../hardware ../hardware/selftest_main.cc \
	    -o hardware_selftest

# ---------------------------------------------------------------------------
# 帧率微基准（主机 x86；不参与 make test，见 make bench）
# 量化：① 谱特征 Jacobi 降频（每 N 帧 1 次）② 预处理单遍零拷贝 vs 旧多遍链路
# ③ 每帧 memcpy 成本。数值是 host 标量口径，板端 NEON 路径见报告。
# ---------------------------------------------------------------------------
BENCH_BIN := $(TEST_DIR)/bench_pipeline

bench: $(BENCH_BIN)

$(BENCH_BIN): $(TEST_DIR)/bench_pipeline.cc $(SRC_DIR)/preprocess.cc \
              include/preprocess.h include/enhanced_decision.h \
              $(SRC_DIR)/enhanced_decision.cc ../algorithms/graph_laplacian.h
	$(CXX) $(CXXFLAGS) $(INC) $(TEST_DIR)/bench_pipeline.cc \
	    $(SRC_DIR)/preprocess.cc $(SRC_DIR)/enhanced_decision.cc -o $(BENCH_BIN)
	./$(BENCH_BIN)

# ---------------------------------------------------------------------------
# 开发板交叉编译（需 Luckfox SDK）
#
# 环境变量：
#   CROSS_COMPILE    交叉编译器前缀（默认 arm-none-linux-gnueabihf-）
#   LUCKFOX_SDK_ROOT Luckfox SDK 根目录（含 include/rknn 头、lib/*.so）
#   OPENCV_ROOT      SDK 自带 OpenCV 前缀（可选）
#
# 实测复核（2026-10-30，Ubuntu 构建机 board_sdk 的 CMake flags.make/link.txt）：
#   -D RV1106_1103 必须定义，否则 rknn_app_context_t 没有 input_mems/output_mems、
#   postprocess.cc 会编到 rknn_output 分支（把 rknn_tensor_mem* 当 rknn_output* 用）。
#   链接需 sample_comm / rockit / rockchip_mpp / rkaiq / rtsp / rga（旧目标只写了
#   -lrknnmrt -lrtsp_demo，无法链接）。
# ---------------------------------------------------------------------------
CROSS_COMPILE    ?= arm-none-linux-gnueabihf-
LUCKFOX_SDK_ROOT ?=
OPENCV_ROOT      ?=

CROSS_CXX         = $(CROSS_COMPILE)g++

CROSS_CXXFLAGS = -std=c++11 -O2 -mfpu=neon-vfpv4 -mfloat-abi=hard -DNDEBUG \
                 -DRV1106_1103 -DISP_HW_V30 -DRKPLATFORM=ON -DARCH64=OFF \
                 -DROCKIVA -DUAPI2 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE \
                 -D_FILE_OFFSET_BITS=64

ifneq ($(strip $(OPENCV_ROOT)),)
OPENCV_INC = -I$(OPENCV_ROOT)/include/opencv4
OPENCV_LIB = -L$(OPENCV_ROOT)/lib
else
OPENCV_INC =
OPENCV_LIB =
endif

SDK_INC = -I$(LUCKFOX_SDK_ROOT) -I$(LUCKFOX_SDK_ROOT)/include \
          -I$(LUCKFOX_SDK_ROOT)/include/rknn -I$(LUCKFOX_SDK_ROOT)/include/rkaiq \
          -I$(LUCKFOX_SDK_ROOT)/include/rkaiq/uAPI2 \
          -I$(LUCKFOX_SDK_ROOT)/include/rkaiq/common \
          -I$(LUCKFOX_SDK_ROOT)/include/rkaiq/xcore \
          -I$(LUCKFOX_SDK_ROOT)/include/rkaiq/algos \
          -I$(LUCKFOX_SDK_ROOT)/include/rkaiq/iq_parser \
          -I$(LUCKFOX_SDK_ROOT)/include/rkaiq/iq_parser_v2 \
          -I$(LUCKFOX_SDK_ROOT)/include/librga -I$(LUCKFOX_SDK_ROOT)/common \
          -I$(LUCKFOX_SDK_ROOT)/common/isp3.x -I$(LUCKFOX_SDK_ROOT)/utils \
          -Iinclude -I../algorithms $(OPENCV_INC)

# main.cc 已默认启用增强决策路径（无需额外编译宏开关）
luckfox:
	@test -n "$(LUCKFOX_SDK_ROOT)" || { echo "错误: 请设置 LUCKFOX_SDK_ROOT"; exit 1; }
	$(CROSS_CXX) $(CROSS_CXXFLAGS) $(SDK_INC) \
	    $(SRC_DIR)/main.cc $(SRC_DIR)/enhanced_decision.cc $(SRC_DIR)/risk_engine.cc \
	    $(SRC_DIR)/yolov5.cc $(SRC_DIR)/postprocess.cc $(SRC_DIR)/luckfox_mpi.cc \
	    $(SRC_DIR)/preprocess.cc \
	    -L$(LUCKFOX_SDK_ROOT)/lib $(OPENCV_LIB) -Wl,-rpath-link,$(LUCKFOX_SDK_ROOT)/lib \
	    -lrknnmrt -lsample_comm -lrockit -lrockchip_mpp -lrkaiq -lrtsp -lrga \
	    -lopencv_core -lopencv_imgproc -lpthread -lrt -ldl -lm \
	    -o luckfox_pico_rtsp_yolov5
	@echo "产物: ./luckfox_pico_rtsp_yolov5（用 CROSS_COMPILE 前缀调用 $(CXX) 即交叉编译）"

clean:
	rm -f $(TEST_DIR)/test_enhanced_decision \
	      $(TEST_DIR)/test_minijson \
	      $(TEST_DIR)/test_risk_config \
	      $(TEST_DIR)/test_golden_regression \
	      $(TEST_DIR)/test_preprocess \
	      $(TEST_DIR)/test_hardware_modules \
	      $(TEST_DIR)/test_selftest \
	      $(TEST_DIR)/test_lcd_menu \
	      $(TEST_DIR)/test_ili9341 \
	      $(TEST_DIR)/test_xpt2046 \
	      $(TEST_DIR)/test_sysfs_spi \
	      $(BENCH_BIN) \
	      hardware_selftest \
	      luckfox_pico_rtsp_yolov5

help:
	@echo "目标:"
	@echo "  make test        编译并运行主机算法单元测试（推荐先跑这个）"
	@echo "  make bench       帧率微基准：谱特征降频 + 预处理合并的前后对比"
	@echo "  make luckfox     交叉编译开发板完整工程（需 LUCKFOX_SDK_ROOT）"
	@echo "  make clean       清理产物"
