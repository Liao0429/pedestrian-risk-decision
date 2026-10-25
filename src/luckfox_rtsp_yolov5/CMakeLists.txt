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

# ---------------------------------------------------------------------------
# 2. 开发板完整工程（交叉编译，需 Luckfox SDK）
# ---------------------------------------------------------------------------
option(BUILD_LUCKFOX_DEMO "Build Luckfox RTSP YOLOv5 demo (requires Luckfox SDK)" OFF)

if(BUILD_LUCKFOX_DEMO)
  if(NOT DEFINED LUCKFOX_SDK_ROOT)
    message(FATAL_ERROR
      "BUILD_LUCKFOX_DEMO=ON 时需要设置 -DLUCKFOX_SDK_ROOT=<Luckfox SDK 根目录>")
  endif()

  add_executable(luckfox_pico_rtsp_yolov5
    ${PROJ_SRC_DIR}/main.cc
    ${PROJ_SRC_DIR}/enhanced_decision.cc
    ${PROJ_SRC_DIR}/risk_engine.cc
    ${PROJ_SRC_DIR}/yolov5.cc
    ${PROJ_SRC_DIR}/postprocess.cc
    ${PROJ_SRC_DIR}/luckfox_mpi.cc
  )
  target_include_directories(luckfox_pico_rtsp_yolov5 PRIVATE
    ${PROJ_INC_DIR} ${ALGO_INC_DIR}
    ${LUCKFOX_SDK_ROOT}/include
    ${LUCKFOX_SDK_ROOT}/3rdparty
  )
  # RKNN / RTSP / rkaiq / OpenCV 链接参数按实际 SDK 布局调整
  target_link_libraries(luckfox_pico_rtsp_yolov5 PRIVATE
    rknnmrt rtsp_demo rkaiq opencv_core opencv_imgproc opencv_imgcodecs
    pthread
  )
  message(STATUS "构建 Luckfox 完整工程（需 SDK 库文件位于 LUCKFOX_SDK_ROOT/lib）")
endif()