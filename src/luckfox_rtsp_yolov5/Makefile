# =============================================================================
# Makefile — 边缘端行人风险决策 C++ 工程构建脚本
# -----------------------------------------------------------------------------
# 两个用途：
#   1) `make test`   在主机（x86）编译并运行算法单元测试，无需 OpenCV/RKNN/RTSP。
#   2) `make luckfox` 交叉编译开发板完整工程（需 Luckfox SDK 环境变量，见下方）。
#
# 依赖：
#   - 主机测试仅需 g++（C++11）。
#   - 交叉编译需要 Luckfox 官方工具链 + RKNN/RTSP/OpenCV 库（见 docs/ARCHITECTURE.md）。
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

.PHONY: all test test-enhanced test-minijson test-risk-config test-golden \
        test-preprocess test-selftest test-lcd-menu clean help luckfox hardware-selftest

all: test

# ---------------------------------------------------------------------------
# 主机单元测试
# ---------------------------------------------------------------------------
test: test-enhanced test-minijson test-risk-config test-golden test-preprocess test-hardware test-selftest test-lcd-menu

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

# 板端硬件自检 CLI（真实 Sysfs 检测，退出码=失败数，供 systemd 门禁）
hardware-selftest: ../hardware/selftest_main.cc ../hardware/selftest.h \
                   ../hardware/gpio.h ../hardware/servo.h
	$(CXX) $(CXXFLAGS) -I../hardware ../hardware/selftest_main.cc \
	    -o hardware_selftest

# ---------------------------------------------------------------------------
# 开发板交叉编译（需 Luckfox SDK）
#
# 环境变量：
#   CROSS_COMPILE 交叉编译器前缀，如 arm-none-linux-gnueabihf-
#   LUCKFOX_SDK_ROOT Luckfox SDK 根目录（含 librknnmrt.so / rtsp / rkaiq）
#   OPENCV_ROOT    板端 OpenCV 安装前缀（可选，默认 SDK 自带）
#
# 说明：完整工程依赖 RKNN 动态库/头文件、RTSP(librtsp_demo) 与 OpenCV。
# 在 Luckfox SDK 内通常用 SDK 顶层 CMake 以 example 方式构建（见 skill
# luckfox-pico-rv1106）。本目标提供等价命令的可追踪模板。
# ---------------------------------------------------------------------------
CROSS_COMPILE    ?= arm-none-linux-gnueabihf-
LUCKFOX_SDK_ROOT ?=
OPENCV_ROOT      ?=
CROSS_CXX         = $(CROSS_COMPILE)g++

# main.cc 已默认启用增强决策路径（无需额外编译宏开关）
luckfox:
	@test -n "$(LUCKFOX_SDK_ROOT)" || { echo "错误: 请设置 LUCKFOX_SDK_ROOT"; exit 1; }
	$(CROSS_CXX) -std=c++11 -O2 \
	    -I$(LUCKFOX_SDK_ROOT)/include \
	    -I$(LUCKFOX_SDK_ROOT)/3rdparty \
	    -I$(OPENCV_ROOT)/include/opencv4 \
	    $(SRC_DIR)/main.cc \
	    $(SRC_DIR)/enhanced_decision.cc \
	    $(SRC_DIR)/risk_engine.cc \
	    $(SRC_DIR)/yolov5.cc \
	    $(SRC_DIR)/postprocess.cc \
	    $(SRC_DIR)/luckfox_mpi.cc \
	    $(SRC_DIR)/preprocess.cc \
	    -L$(LUCKFOX_SDK_ROOT)/lib -lrknnmrt -lrtsp_demo -lopencv_core -lopencv_imgproc \
	    -lpthread -o luckfox_pico_rtsp_yolov5
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
	      hardware_selftest \
	      luckfox_pico_rtsp_yolov5

help:
	@echo "目标:"
	@echo "  make test        编译并运行主机算法单元测试（推荐先跑这个）"
	@echo "  make luckfox     交叉编译开发板完整工程（需 LUCKFOX_SDK_ROOT）"
	@echo "  make clean       清理产物"