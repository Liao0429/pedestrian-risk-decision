// =============================================================================
// selftest.h — 硬件自检框架（开机自检：GPIO/PWM/SPI 屏幕/USB 声卡/WiFi/按键）
// -----------------------------------------------------------------------------
// 用途：答辩硬件展示前逐项检测外设可用性，每项输出 PASS/FAIL，全部 PASS
//       才允许进入演示模式（接入 systemd 开机自检 + start_demo.sh 门禁）。
//
// 检测项（与 docs/hardware/selftest.md 对应）：
//   1. GPIO_LOOPBACK : GPIO 写读回（输出写高/写低后读回校验）
//   2. PWM_NODE      : PWM 节点可导出、可配置、可写 duty（舵机云台通道）
//   3. SPI_LCD_ID    : SPI 屏幕设备节点存在且 ID 匹配 0x9341（ILI9341）
//   4. USB_AUDIO     : aplay 可用且检测到 USB 声卡（USB 喇叭）
//   5. WIFI_LINK     : wlan0 存在并已获取 IP（手机热点连接）
//   6. BUTTON_LEVEL  : 按键 GPIO 可配置为输入并可读电平
//
// 设计（与 gpio.h/servo.h 同风格，全部可在 PC/CI 上测试）：
//   - Gpio/Pwm 复用现有抽象：板端传 SysfsGpio/SysfsPwm，测试传 Fake；
//   - SelftestProbe 抽象负责"文件存在性 + 命令输出抓取"：板端
//     ShellSelftestProbe（access/popen），测试 FakeSelftestProbe 注入结果；
//   - HardwareSelfTest 聚合执行 6 项检测，生成 SelftestResult 数组并统计
//     PASS/FAIL；任何一项失败都不影响其余项继续检测。
//
// 集成示例（板端 CLI 见 selftest_main.cc）：
//   hw::SysfsGpio  loopGpio(56);            // LED 红（写读回）
//   hw::SysfsGpio  btnGpio(92);             // 按键
//   hw::SysfsPwm   pwm(0, 0);               // 云台 PAN
//   hw::ShellSelftestProbe probe;
//   hw::HardwareSelfTest st(&loopGpio, &pwm, &probe);
//   st.setButtonGpio(&btnGpio);
//   st.runAll();
//   st.printReport();
//   return st.allPassed() ? 0 : 1;
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "gpio.h"
#include "servo.h"

namespace hw {

// -----------------------------------------------------------------------------
// 自检项枚举与名称
// -----------------------------------------------------------------------------
enum class SelftestItem : uint8_t {
    GPIO_LOOPBACK = 0,  // GPIO 写读回
    PWM_NODE,           // PWM 节点可导出/配置
    SPI_LCD_ID,         // SPI 屏幕 ID == 0x9341
    USB_AUDIO,          // USB 声卡可用
    WIFI_LINK,          // wlan0 已获取 IP
    BUTTON_LEVEL,       // 按键 GPIO 输入可读
    COUNT               // 总项数（保持最后）
};

inline const char* selftestItemName(SelftestItem item) {
    switch (item) {
        case SelftestItem::GPIO_LOOPBACK: return "GPIO_LOOPBACK";
        case SelftestItem::PWM_NODE:      return "PWM_NODE";
        case SelftestItem::SPI_LCD_ID:    return "SPI_LCD_ID";
        case SelftestItem::USB_AUDIO:     return "USB_AUDIO";
        case SelftestItem::WIFI_LINK:     return "WIFI_LINK";
        case SelftestItem::BUTTON_LEVEL:  return "BUTTON_LEVEL";
        default:                          return "UNKNOWN";
    }
}

// -----------------------------------------------------------------------------
// 单项自检结果
// -----------------------------------------------------------------------------
struct SelftestResult {
    SelftestItem item;
    bool         pass;
    char         detail[192];

    SelftestResult() : item(SelftestItem::GPIO_LOOPBACK), pass(false) {
        detail[0] = '\0';
    }
};

// -----------------------------------------------------------------------------
// SelftestProbe — 系统探测抽象（文件存在性 + 命令输出）
//   板端实现：ShellSelftestProbe（access + popen）
//   测试实现：FakeSelftestProbe（内存注入）
// -----------------------------------------------------------------------------
class SelftestProbe {
public:
    virtual ~SelftestProbe() {}

    // 文件/设备节点是否存在
    virtual bool fileExists(const char* path) const = 0;

    // 运行命令并抓取 stdout 到 out（保证 NUL 结尾）。
    // 返回 true 表示命令执行成功且有输出。
    virtual bool runCmd(const char* cmd, char* out, size_t outSize) const = 0;
};

// 板端探测：Linux 文件系统 + popen
class ShellSelftestProbe : public SelftestProbe {
public:
    bool fileExists(const char* path) const override {
        return access(path, F_OK) == 0;
    }

    bool runCmd(const char* cmd, char* out, size_t outSize) const override {
        if (!cmd || !out || outSize == 0) return false;
        out[0] = '\0';
        FILE* p = popen(cmd, "r");
        if (!p) return false;
        size_t n = fread(out, 1, outSize - 1, p);
        int rc = pclose(p);
        out[(n < outSize) ? n : outSize - 1] = '\0';
        return rc == 0 && n > 0;
    }
};

// 测试探测：内存注入文件存在性与命令输出
class FakeSelftestProbe : public SelftestProbe {
public:
    void setFile(const std::string& path, bool exists) {
        m_files[path] = exists;
    }

    void setCmdOutput(const std::string& cmd, const std::string& out) {
        m_cmds[cmd] = out;
    }

    bool fileExists(const char* path) const override {
        std::map<std::string, bool>::const_iterator it =
            m_files.find(path ? path : "");
        return it != m_files.end() ? it->second : false;
    }

    bool runCmd(const char* cmd, char* out, size_t outSize) const override {
        if (!out || outSize == 0) return false;
        out[0] = '\0';
        std::map<std::string, std::string>::const_iterator it =
            m_cmds.find(cmd ? cmd : "");
        if (it == m_cmds.end()) return false;
        size_t n = it->second.size();
        if (n >= outSize) n = outSize - 1;
        memcpy(out, it->second.c_str(), n);
        out[n] = '\0';
        return true;
    }

private:
    std::map<std::string, bool>           m_files;
    std::map<std::string, std::string>    m_cmds;
};

// -----------------------------------------------------------------------------
// HardwareSelfTest — 硬件自检执行器
//   构造注入：GPIO（写读回用）、PWM（云台通道）、Probe（系统探测）。
//   按键 GPIO 可单独设置（setButtonGpio），默认与写读回共用同一 Gpio。
//   runAll() 逐项检测，单项失败不中断；结果可通过 result(i)/计数接口查询。
// -----------------------------------------------------------------------------
class HardwareSelfTest {
public:
    // gpio    : 用于 GPIO 写读回的 Gpio（板端 SysfsGpio，测试 FakeGpio）
    // pwm     : 云台 PWM 通道（板端 SysfsPwm，测试 FakePwm/BadPwm）
    // probe   : 系统探测（文件 + 命令）
    HardwareSelfTest(Gpio* gpio, Pwm* pwm, SelftestProbe* probe)
        : m_gpio(gpio),
          m_btnGpio(gpio),
          m_pwm(pwm),
          m_probe(probe),
          m_passed(0),
          m_failed(0),
          m_run(false) {}

    // 配置项（均有默认值，按实测环境覆盖）

    // 按键 GPIO 与写读回 GPIO 不同时，单独设置
    void setButtonGpio(Gpio* gpio) { m_btnGpio = gpio; }

    // SPI 设备节点（默认 /dev/spidev0.0）
    void setSpiDevice(const char* dev) {
        m_spiDev = dev ? dev : "/dev/spidev0.0";
    }

    // SPI 屏幕 ID 探测命令：输出需包含 "9341"
    // 默认读取设备树 compatible；接入 LVGL 后可换成真实 SPI 读 ID 工具。
    void setSpiIdCmd(const char* cmd) {
        if (cmd && cmd[0]) m_spiCmd = cmd;
    }

    // USB 声卡探测命令：输出需包含 "USB"（aplay -l 显示 USB Audio）
    void setAudioProbeCmd(const char* cmd) {
        if (cmd && cmd[0]) m_audioCmd = cmd;
    }

    // WiFi 探测命令：输出需包含 "inet"（wlan0 已获取 IP）
    void setWifiProbeCmd(const char* cmd) {
        if (cmd && cmd[0]) m_wifiCmd = cmd;
    }

    // 执行全部自检项。返回是否全部通过。
    bool runAll() {
        m_passed = 0;
        m_failed = 0;
        m_run = true;
        checkGpioLoopback();
        checkPwmNode();
        checkSpiLcdId();
        checkUsbAudio();
        checkWifiLink();
        checkButtonLevel();
        return allPassed();
    }

    bool allPassed() const { return m_run && m_failed == 0; }
    int  passedCount() const { return m_passed; }
    int  failedCount() const { return m_failed; }

    // 单项结果（i 范围 0..COUNT-1）
    const SelftestResult& result(int i) const {
        return m_results[i];
    }

    // 打印完整报告（PASS/FAIL 表格）
    void printReport(FILE* f = stdout) const {
        for (int i = 0; i < (int)SelftestItem::COUNT; ++i) {
            const SelftestResult& r = m_results[i];
            std::fprintf(f, "[%s] %-14s %s\n",
                         r.pass ? "PASS" : "FAIL",
                         selftestItemName(r.item), r.detail);
        }
    }

private:
    SelftestResult& resultFor(SelftestItem item) {
        return m_results[(int)item];
    }

    void passItem(SelftestItem item, const char* detail) {
        SelftestResult& r = resultFor(item);
        r.item = item;
        r.pass = true;
        std::snprintf(r.detail, sizeof(r.detail), "%s", detail);
        ++m_passed;
    }

    void failItem(SelftestItem item, const char* detail) {
        SelftestResult& r = resultFor(item);
        r.item = item;
        r.pass = false;
        std::snprintf(r.detail, sizeof(r.detail), "%s", detail);
        ++m_failed;
    }

    // 1. GPIO 写读回：输出写高读回、写低读回
    void checkGpioLoopback() {
        if (!m_gpio) {
            failItem(SelftestItem::GPIO_LOOPBACK, "GPIO 对象为空");
            return;
        }
        if (!m_gpio->setDirection(true)) {
            failItem(SelftestItem::GPIO_LOOPBACK, "setDirection(out) 失败");
            return;
        }
        bool v = false;
        m_gpio->write(true);
        if (!m_gpio->read(&v) || !v) {
            failItem(SelftestItem::GPIO_LOOPBACK, "写高读回失败");
            return;
        }
        m_gpio->write(false);
        if (!m_gpio->read(&v) || v) {
            failItem(SelftestItem::GPIO_LOOPBACK, "写低读回失败");
            return;
        }
        passItem(SelftestItem::GPIO_LOOPBACK, "GPIO 输出写读回 OK");
    }

    // 2. PWM 节点：导出 -> 配置 20ms 周期 -> 使能 -> 写中位 duty -> 关闭
    //    注：自检会使舵机短暂动作（若已接），属预期行为。
    void checkPwmNode() {
        if (!m_pwm) {
            failItem(SelftestItem::PWM_NODE, "PWM 对象为空");
            return;
        }
        if (!m_pwm->configure(20000000L)) {
            failItem(SelftestItem::PWM_NODE, "PWM configure(20ms) 失败");
            return;
        }
        if (!m_pwm->enable(true)) {
            failItem(SelftestItem::PWM_NODE, "PWM enable 失败");
            return;
        }
        if (!m_pwm->setDutyCycle(1500000L)) {
            failItem(SelftestItem::PWM_NODE, "PWM 写 duty(1.5ms) 失败");
            return;
        }
        m_pwm->enable(false);
        passItem(SelftestItem::PWM_NODE, "PWM 节点导出/配置/写 duty OK");
    }

    // 3. SPI 屏幕 ID：设备节点存在 + 探测命令输出含 "9341"
    void checkSpiLcdId() {
        if (!m_probe) {
            failItem(SelftestItem::SPI_LCD_ID, "SelftestProbe 为空");
            return;
        }
        if (!m_probe->fileExists(m_spiDev.c_str())) {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "未找到 SPI 设备节点 %s", m_spiDev.c_str());
            failItem(SelftestItem::SPI_LCD_ID, buf);
            return;
        }
        char out[160];
        if (!m_probe->runCmd(m_spiCmd.c_str(), out, sizeof(out))) {
            failItem(SelftestItem::SPI_LCD_ID, "SPI ID 探测命令无输出");
            return;
        }
        if (!strstr(out, "9341")) {
            failItem(SelftestItem::SPI_LCD_ID, "LCD ID 不匹配 0x9341");
            return;
        }
        passItem(SelftestItem::SPI_LCD_ID, "SPI 屏幕 ID = 0x9341 (ILI9341)");
    }

    // 4. USB 声卡：aplay 可用且输出含 "USB"
    void checkUsbAudio() {
        if (!m_probe) {
            failItem(SelftestItem::USB_AUDIO, "SelftestProbe 为空");
            return;
        }
        char out[256];
        if (!m_probe->runCmd(m_audioCmd.c_str(), out, sizeof(out))) {
            failItem(SelftestItem::USB_AUDIO,
                     "aplay 无输出（aplay 缺失或无声卡）");
            return;
        }
        if (!strstr(out, "USB")) {
            failItem(SelftestItem::USB_AUDIO, "未检测到 USB 声卡");
            return;
        }
        passItem(SelftestItem::USB_AUDIO, "USB 声卡可用 (aplay)");
    }

    // 5. WiFi 连接：wlan0 存在且有 IPv4 地址
    void checkWifiLink() {
        if (!m_probe) {
            failItem(SelftestItem::WIFI_LINK, "SelftestProbe 为空");
            return;
        }
        char out[256];
        if (!m_probe->runCmd(m_wifiCmd.c_str(), out, sizeof(out))) {
            failItem(SelftestItem::WIFI_LINK, "wlan0 不存在或未就绪");
            return;
        }
        if (!strstr(out, "inet")) {
            failItem(SelftestItem::WIFI_LINK, "wlan0 未获取 IP");
            return;
        }
        passItem(SelftestItem::WIFI_LINK, "wlan0 已连接并获取 IP");
    }

    // 6. 按键电平：配置为输入并读取一次电平
    void checkButtonLevel() {
        if (!m_btnGpio) {
            failItem(SelftestItem::BUTTON_LEVEL, "按键 GPIO 对象为空");
            return;
        }
        if (!m_btnGpio->setDirection(false)) {
            failItem(SelftestItem::BUTTON_LEVEL, "setDirection(in) 失败");
            return;
        }
        bool v = false;
        if (!m_btnGpio->read(&v)) {
            failItem(SelftestItem::BUTTON_LEVEL, "按键 GPIO 读取失败");
            return;
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "按键电平读取 OK（当前 %s）",
                      v ? "HIGH" : "LOW");
        passItem(SelftestItem::BUTTON_LEVEL, buf);
    }

    Gpio*           m_gpio;
    Gpio*           m_btnGpio;
    Pwm*            m_pwm;
    SelftestProbe*  m_probe;

    std::string m_spiDev    = "/dev/spidev0.0";
    std::string m_spiCmd    = "cat /sys/bus/spi/devices/spi0.0/of_node/compatible 2>/dev/null";
    std::string m_audioCmd  = "aplay -l 2>/dev/null";
    std::string m_wifiCmd   = "ip -o -4 addr show wlan0 2>/dev/null";

    SelftestResult m_results[(int)SelftestItem::COUNT];
    int  m_passed;
    int  m_failed;
    bool m_run;
};

}  // namespace hw