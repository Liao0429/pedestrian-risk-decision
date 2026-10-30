// =============================================================================
// selftest_main.cc — 硬件自检命令行工具（板端 CLI）
// -----------------------------------------------------------------------------
// 用法（开发板/任意 Linux）：
//   ./hardware_selftest [-g <loopback_gpio>] [-b <button_gpio>]
//                       [-c <pwm_chip>] [-p <pwm_channel>]
// 默认值（2026-10-30 于 Luckfox Pico Max 实机校准：ls /sys/class/pwm、
// cat /sys/kernel/debug/gpio）：
//   -g 56    GPIO 写读回引脚（GPIO1_D0，UART3_TX 预留脚；打样后请换实测空闲脚）
//   -b 67    按键 GPIO（GPIO2_A3_d = sysfs 67，对应排针 Pin27 / PCB J8 跳线目标）
//   -c 8     PWM chip（PAN=Yaw = PWM8_M1；板上只有 pwmchip8/pwmchip9）
//   -p 0     PWM 通道（每个 pwmchip 只有 1 通道 → 必须 0；TILT=Pitch 用 -c 9）
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
                 "  默认: -g 56 -b 67 -c 8 -p 0\n"
                 "  退出码: 0=全部 PASS, 1=存在 FAIL\n");
}

}  // namespace

int main(int argc, char** argv) {
    // 2026-10-30 实机校准：旧值 -b 92 是错误的——sysfs 92 = GPIO2_D4（gpiochip2
    // base=64 实测），与 GPIO1_C3 / 排针 Pin27 都无关；旧值 -c 0 也不存在（板上仅
    // pwmchip8/pwmchip9，npwm=1）。TILT(Pitch) 舵机请用 -c 9 -p 0。
    int loopGpio = 56;   // GPIO1_D0（UART3_TX 预留脚，打样后按实测替换）
    int btnGpio  = 67;   // 按键 GPIO2_A3_d（排针 Pin 27，PCB J8 SW_ALARM）
    int pwmChip  = 8;    // 云台 PAN = PWM8_M1（GPIO1_C4_d）
    int pwmChan  = 0;    // 每个 pwmchip 只有 1 个通道

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

    // 清理现场：自检会 export GPIO/PWM，退出前取消导出，避免残留占用
    // （2026-10-30 真机实跑：不清理会在 /sys/class/gpio 留下 gpio56/gpio67）。
    loopGpioObj.unexportPin();
    btnGpioObj.unexportPin();
    pwmObj.unexportPin();

    std::printf("=== result: %d PASS / %d FAIL ===\n",
                selftest.passedCount(), selftest.failedCount());
    return selftest.allPassed() ? 0 : 1;
}