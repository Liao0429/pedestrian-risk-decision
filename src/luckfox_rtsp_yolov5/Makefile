# =============================================================================
# Makefile — 边缘端行人风险决策 C++ 工程构建脚本
# -----------------------------------------------------------------------------
# 两个用途：
#   1) `make test`   在主机（x86）编译并运行算法单元测试，无需 OpenCV/RKNN/RTSP。
#   2) `make luckfox` 交叉编译开发板完整工程（需 Luckfox SDK 环境变量，见下方）。
#
# 依赖：
#   - 主机测试仅需 g++（C++11）。
#   - 交叉编译需要 Luckfox 官方工具链 + RKNN/RTSP/OpenCV 库（见 docs/INTEGRATION_PLAN.md）。
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

.PHONY: all test test-enhanced test-minijson test-risk-config \
        clean help luckfox

all: test

# ---------------------------------------------------------------------------
# 主机单元测试
# ---------------------------------------------------------------------------
test: test-enhanced test-minijson test-risk-config

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
	    -L$(LUCKFOX_SDK_ROOT)/lib -lrknnmrt -lrtsp_demo -lopencv_core -lopencv_imgproc \
	    -lpthread -o luckfox_pico_rtsp_yolov5
	@echo "产物: ./luckfox_pico_rtsp_yolov5（用 CROSS_COMPILE 前缀调用 $(CXX) 即交叉编译）"

clean:
	rm -f $(TEST_DIR)/test_enhanced_decision \
	      $(TEST_DIR)/test_minijson \
	      $(TEST_DIR)/test_risk_config \
	      luckfox_pico_rtsp_yolov5

help:
	@echo "目标:"
	@echo "  make test        编译并运行主机算法单元测试（推荐先跑这个）"
	@echo "  make luckfox     交叉编译开发板完整工程（需 LUCKFOX_SDK_ROOT）"
	@echo "  make clean       清理产物"