#!/bin/bash
set -e
SDK=/home/liao/luckfox/luckfox_pico_rkmpi_example-kernel-5.10.160
CXX=/home/liao/luckfox/gcc-arm-11.2-2022.02-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-g++
mkdir -p /tmp/o2build
INC="-I$SDK/example/luckfox_pico_rtsp_yolov5/include -I$SDK -I$SDK/utils -I$SDK/common -I$SDK/common/isp3.x -I$SDK/include -I$SDK/include/rknn -I$SDK/include/librga -I$SDK/include/rkaiq -I$SDK/include/rkaiq/uAPI2 -I$SDK/include/rkaiq/common -I$SDK/include/rkaiq/xcore -I$SDK/include/rkaiq/algos -I$SDK/include/rkaiq/iq_parser -I$SDK/include/rkaiq/iq_parser_v2 -I$SDK/include/rkaiq/smartIr -I$SDK/include/opencv4"
DEFS="-DRV1106_1103 -DISP_HW_V30 -DRKPLATFORM=ON -DARCH64=OFF -DUAPI2 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"
cd $SDK/example/luckfox_pico_rtsp_yolov5
for f in src/main.cc src/enhanced_decision.cc src/luckfox_mpi.cc src/postprocess.cc src/risk_engine.cc src/yolov5.cc; do
  obj=/tmp/o2build/$(basename $f .cc).o
  echo "编译 $f -> $obj"
  $CXX -std=c++11 -O2 $DEFS $INC -c $f -o $obj
done
echo "=== 链接 ==="
$CXX -Wl,-rpath-link,$SDK/lib:/usr/lib /tmp/o2build/enhanced_decision.o /tmp/o2build/luckfox_mpi.o /tmp/o2build/main.o /tmp/o2build/postprocess.o /tmp/o2build/risk_engine.o /tmp/o2build/yolov5.o -o /tmp/luckfox_o2 \
  -L$SDK/lib/glibc -Wl,-rpath,$SDK/lib/glibc: $SDK/lib/glibc/lib/libopencv_core.a $SDK/lib/glibc/lib/libopencv_features2d.a $SDK/lib/glibc/lib/libopencv_highgui.a $SDK/lib/glibc/lib/libopencv_imgproc.a $SDK/lib/glibc/lib/libopencv_photo.a $SDK/lib/glibc/lib/libopencv_video.a \
  -lrknnmrt -lsample_comm -lrockit -lrockchip_mpp -lrkaiq -lpthread -lrtsp -lstdc++ -lfunc_none -lc -lm -lrt -ldl -lrga $SDK/lib/glibc/lib/libopencv_imgproc.a $SDK/lib/glibc/lib/libopencv_core.a -lpthread -lm -lrt -ldl
echo "=== 完成 ==="
ls -la /tmp/luckfox_o2
