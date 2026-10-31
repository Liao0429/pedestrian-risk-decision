cmake_minimum_required(VERSION 3.10)
project(pedestrian_risk_decision LANGUAGES CXX)

# =============================================================================
# CMakeLists.txt — 边缘端行人风险决策 C++ 工程
# -----------------------------------------------------------------------------
# 默认构建主机算法单元测试（无需 OpenCV/RKNN/RTSP，g++ 即可）：
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
#
# 交叉编译开发板完整工程（需 Luckfox SDK）：
#   cmake -S . -B build -DBUILD_LUCKFOX_DEMO=ON \
#         -DLUCKFOX_SDK_ROOT=/path/to/luckfox/sdk \
#         -DCMAKE_CXX_COMPILER=arm-none-linux-gnueabihf-g++
#   cmake --build build
# =============================================================================

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(PROJ_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(PROJ_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(ALGO_INC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../algorithms)

enable_testing()

# ---------------------------------------------------------------------------
# 1. 主机算法单元测试（本仓库核心算法，无第三方依赖）
# ---------------------------------------------------------------------------

add_executable(test_enhanced_decision
  ${PROJ_SRC_DIR}/enhanced_decision.cc
  tests/test_enhanced_decision.cc
)
target_include_directories(test_enhanced_decision PRIVATE
  ${PROJ_INC_DIR} ${ALGO_INC_DIR})
add_test(NAME test_enhanced_decision COMMAND test_enhanced_decision)

add_executable(test_minijson
  tests/test_minijson.cc
)
target_include_directories(test_minijson PRIVATE ${ALGO_INC_DIR})
add_test(NAME test_minijson COMMAND test_minijson)

# test_risk_config：若找到 OpenCV 则连同 risk_engine.cc 一起编译并验证
# RiskEngine::setConfig 生效；否则只验证 risk_config 的 JSON 读取逻辑。
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
  add_executable(test_risk_config
    ${PROJ_SRC_DIR}/risk_engine.cc
    tests/test_risk_config.cc
  )
  target_include_directories(test_risk_config PRIVATE
    ${PROJ_INC_DIR} ${ALGO_INC_DIR} ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(test_risk_config PRIVATE ${OpenCV_LIBS})
else()
  add_executable(test_risk_config
    tests/test_risk_config.cc
  )
  target_include_directories(test_risk_config PRIVATE
    ${PROJ_INC_DIR} ${ALGO_INC_DIR})
endif()
add_test(NAME test_risk_config COMMAND test_risk_config)

# 硬件驱动模块主机测试：三色 LED 颜色映射 + 按键防抖/边沿检测 +
# 舵机角度换算 + 云台跟踪逻辑（使用 FakeGpio/FakePwm 内存替身，
# 无 Linux/GPIO 依赖，PC/CI 可直接运行）
add_executable(test_hardware_modules
  tests/test_hardware_modules.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/voice_alert.cc
)
target_include_directories(test_hardware_modules PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
add_test(NAME test_hardware_modules COMMAND test_hardware_modules)

# 黄金回归：C++ 实现读取 tests/golden/*.json（scripts/generate_golden.py 用
# Python 参考实现生成），防止 C++/Python 双实现漂移。显式传入绝对 golden 目录，
# 因为 ctest 的工作目录是 build 目录。
add_executable(test_golden_regression
  tests/test_golden_regression.cc
)
target_include_directories(test_golden_regression PRIVATE
  ${PROJ_INC_DIR} ${ALGO_INC_DIR})
add_test(NAME test_golden_regression
  COMMAND test_golden_regression
          ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/golden)

# ---------------------------------------------------------------------------
# 2. 开发板完整工程（交叉编译，需 Luckfox SDK）
# ---------------------------------------------------------------------------
option(BUILD_LUCKFOX_DEMO "Build Luckfox RTSP YOLOv5 demo (requires Luckfox SDK)" OFF)
# 触摸画框 UI（ILI9341 + XPT2046 + RoiDraw）：默认 OFF。
# 打开后额外编译 touch_roi_bridge.cc 与 ../hardware/sysfs_spi.cc，并定义
# BOARD_TOUCH_ROI=1；运行时仍需 config 的 touch_roi_enabled=true 才会初始化硬件。
option(BOARD_TOUCH_ROI "Enable ILI9341/XPT2046 touch ROI drawing UI" OFF)

if(BUILD_LUCKFOX_DEMO)
  if(NOT DEFINED LUCKFOX_SDK_ROOT)
    message(FATAL_ERROR
      "BUILD_LUCKFOX_DEMO=ON 时需要设置 -DLUCKFOX_SDK_ROOT=<Luckfox SDK 根目录>")
  endif()

  # main.cc 已默认启用增强决策路径（无需额外编译宏开关）
  add_executable(luckfox_pico_rtsp_yolov5
    ${PROJ_SRC_DIR}/main.cc
    ${PROJ_SRC_DIR}/enhanced_decision.cc
    ${PROJ_SRC_DIR}/risk_engine.cc
    ${PROJ_SRC_DIR}/yolov5.cc
    ${PROJ_SRC_DIR}/postprocess.cc
    ${PROJ_SRC_DIR}/luckfox_mpi.cc
    ${PROJ_SRC_DIR}/preprocess.cc
  )
  # yolov5.h / postprocess.h 来自 SDK 的 include/rknn；RV1106_1103 决定
  # rknn_app_context_t 是否带 input_mems/output_mems，以及 postprocess.cc 走哪条
  # int8 解码分支，必须定义（与 Ubuntu 构建机实测成功的 board_sdk CMake 一致）。
  target_include_directories(luckfox_pico_rtsp_yolov5 PRIVATE
    ${PROJ_INC_DIR} ${ALGO_INC_DIR}
    ${LUCKFOX_SDK_ROOT}
    ${LUCKFOX_SDK_ROOT}/include
    ${LUCKFOX_SDK_ROOT}/include/rknn
    ${LUCKFOX_SDK_ROOT}/include/rkaiq
    ${LUCKFOX_SDK_ROOT}/include/rkaiq/uAPI2
    ${LUCKFOX_SDK_ROOT}/include/rkaiq/common
    ${LUCKFOX_SDK_ROOT}/include/rkaiq/xcore
    ${LUCKFOX_SDK_ROOT}/include/rkaiq/algos
    ${LUCKFOX_SDK_ROOT}/include/rkaiq/iq_parser
    ${LUCKFOX_SDK_ROOT}/include/rkaiq/iq_parser_v2
    ${LUCKFOX_SDK_ROOT}/include/librga
    ${LUCKFOX_SDK_ROOT}/common
    ${LUCKFOX_SDK_ROOT}/common/isp3.x
    ${LUCKFOX_SDK_ROOT}/utils
  )
  target_compile_definitions(luckfox_pico_rtsp_yolov5 PRIVATE
    RV1106_1103 ISP_HW_V30 RKPLATFORM=ON ARCH64=OFF ROCKIVA UAPI2
    _LARGEFILE_SOURCE _LARGEFILE64_SOURCE _FILE_OFFSET_BITS=64)
  target_compile_options(luckfox_pico_rtsp_yolov5 PRIVATE
    -O2 -mfpu=neon-vfpv4 -mfloat-abi=hard)
  if(BOARD_TOUCH_ROI)
    target_sources(luckfox_pico_rtsp_yolov5 PRIVATE
      ${PROJ_SRC_DIR}/touch_roi_bridge.cc
      ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/sysfs_spi.cc)
    target_include_directories(luckfox_pico_rtsp_yolov5 PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
    target_compile_definitions(luckfox_pico_rtsp_yolov5 PRIVATE BOARD_TOUCH_ROI=1)
    message(STATUS "触摸画框 UI 已启用（BOARD_TOUCH_ROI=1）：额外编译 touch_roi_bridge.cc + sysfs_spi.cc")
  else()
    target_compile_definitions(luckfox_pico_rtsp_yolov5 PRIVATE BOARD_TOUCH_ROI=0)
  endif()
  # RKNN / RTSP / rkaiq / OpenCV 链接参数按实际 SDK 布局调整
  target_link_libraries(luckfox_pico_rtsp_yolov5 PRIVATE
    rknnmrt sample_comm rockit rockchip_mpp rkaiq rtsp rga
    opencv_core opencv_imgproc pthread rt dl m
  )
  message(STATUS "构建 Luckfox 完整工程（需 SDK 库文件位于 LUCKFOX_SDK_ROOT/lib）")
endif()