// =============================================================================
// selftest_main.cc — 硬件自检命令行工具（板端 CLI）
// -----------------------------------------------------------------------------
// 用法（开发板/任意 Linux）：
//   ./hardware_selftest [-g <loopback_gpio>] [-b <button_gpio>]
//                       [-c <pwm_chip>] [-p <pwm_channel>]
// 默认值（以实测 /sys/class/gpio、/sys/class/pwm 为准）：
//   -g 56    GPIO 写读回引脚（默认 LED 红，打样后按实测调整）
//   -b 92    按键 GPIO（GPIO1_C3_d，Pin 92）
//   -c 0     PWM chip（云台 PAN）
//   -p 0     PWM 通道
//
// 退出码：0 = 全部 PASS，1 = 存在 FAIL（供 systemd/脚本门禁判断）。
//
// 编译（主机/板端通用，无第三方依赖）：
//   g++ -std=c++11 -O2 -Wall -Wextra -I. selftest_main.cc -o hardware_selftest
//   （或用 Makefile 的 `make hardware-selftest` 目标）
// =============================================================================
#include <cstdio>
#include <cstring>

#include "gpio.h"
#include "selftest.h"
#include "servo.h"

namespace {

void usage(const char* prog) {
    std::fprintf(stderr,
                 "用法: %s [-g <loopback_gpio>] [-b <button_gpio>] "
                 "[-c <pwm_chip>] [-p <pwm_channel>]\n",
                 prog);
    std::fprintf(stderr,
                 "  默认: -g 56 -b 92 -c 0 -p 0\n"
                 "  退出码: 0=全部 PASS, 1=存在 FAIL\n");
}

}  // namespace

int main(int argc, char** argv) {
    int loopGpio = 56;   // LED 红（写读回）
    int btnGpio  = 92;   // 按键 GPIO1_C3_d
    int pwmChip  = 0;     // 云台 PAN
    int pwmChan  = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            loopGpio = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            btnGpio = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            pwmChip = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pwmChan = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    std::printf("=== hardware selftest ===\n");
    std::printf("GPIO loopback=%d button=%d PWM=pwmchip%d/pwm%d\n",
                loopGpio, btnGpio, pwmChip, pwmChan);

    hw::SysfsGpio loopGpioObj(loopGpio);
    hw::SysfsGpio btnGpioObj(btnGpio);
    hw::SysfsPwm  pwmObj(pwmChip, pwmChan);
    hw::ShellSelftestProbe probe;

    hw::HardwareSelfTest selftest(&loopGpioObj, &pwmObj, &probe);
    selftest.setButtonGpio(&btnGpioObj);
    selftest.runAll();
    selftest.printReport();

    std::printf("=== result: %d PASS / %d FAIL ===\n",
                selftest.passedCount(), selftest.failedCount());
    return selftest.allPassed() ? 0 : 1;
}