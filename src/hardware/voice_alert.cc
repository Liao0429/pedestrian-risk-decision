// =============================================================================
// voice_alert.cc — AplayVoiceBackend 板端实现（USB 喇叭 + aplay）
// -----------------------------------------------------------------------------
// 对应 docs/hardware/voice_module.md 方案 E：
//   - 免驱 USB 喇叭被内核识别为 ALSA 声卡（常见 plughw:1,0，可用 aplay -l 查）；
//   - 预录告警音频放在系统 TF 卡 /userdata/voice/（warning.wav / danger.wav）；
//   - 通过 system("aplay -q -D <dev> <wav> &") 异步后台播放，不阻塞主循环。
//
// 容错设计：
//   - init() 检查 aplay 是否存在于 PATH、音频文件是否就绪；任一缺失返回 false，
//     仅禁用语音模块，不影响主程序其余功能；
//   - 播放失败（aplay 退出码非 0）只打一行 stderr，不崩溃。
// =============================================================================
#include "voice_alert.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace hw {

// =============================================================================
// AplayVoiceBackend — 真实板端后端
// =============================================================================
class AplayVoiceBackend : public VoiceBackend {
public:
    // device    ALSA 播放设备，默认 plughw:1,0（USB 喇叭，以 aplay -l 实测为准）
    // audioDir  音频文件目录，默认 /userdata/voice/（系统 TF 卡）
    AplayVoiceBackend(const char* device = "plughw:1,0",
                      const char* audioDir = "/userdata/voice/")
        : m_device(device), m_audioDir(audioDir), m_ok(false) {}

    // 检查 aplay 与音频文件是否就绪。失败仅禁用语音，不致命。
    //
    // 2026-10-30 板上实测：Luckfox Pico Max 镜像里**只有 card0 = rv-acodec**
    // （/proc/asound/cards），且没有任何 USB 设备（/sys/bus/usb/devices 为空、
    // USB 控制器是 gadget 模式 /sys/class/udc=ffb00000.usb）。因此旧的默认
    // plughw:1,0 必然打不开；这里在 init() 里自动回退到 ALSA "default"，
    // 并把实际使用的设备打印出来（用 USB 喇叭前必须先让板子进 USB host 模式）。
    bool init() {
        // 1. aplay 是否可用（git-bash/交叉环境可能没有，容错）
        if (system("command -v aplay >/dev/null 2>&1") != 0) {
            std::fprintf(stderr, "voice: aplay not found, voice alert disabled\n");
            m_ok = false;
            return false;
        }
        // 1.5 配置的 ALSA 设备若不存在（如 plughw:1,0 而板上只有 card0），回退 default
        if (!cardPresent()) {
            std::fprintf(stderr,
                         "voice: device %s not present, falling back to 'default'\n",
                         m_device.c_str());
            m_device = "default";
        }
        std::fprintf(stderr, "voice: aplay device = %s\n", m_device.c_str());
        // 2. 音频文件是否存在
        if (access(warningPath().c_str(), F_OK) != 0 ||
            access(dangerPath().c_str(), F_OK) != 0) {
            std::fprintf(stderr,
                         "voice: audio file missing under %s, voice alert disabled\n",
                         m_audioDir.c_str());
            m_ok = false;
            return false;
        }
        m_ok = true;
        return true;
    }

    bool available() const override { return m_ok; }

    void playWarning() override {
        if (!m_ok) return;
        if (system(cmd("warning.wav").c_str()) != 0) {
            std::fprintf(stderr, "voice: aplay warning.wav failed\n");
        }
    }

    void playDanger() override {
        if (!m_ok) return;
        if (system(cmd("danger.wav").c_str()) != 0) {
            std::fprintf(stderr, "voice: aplay danger.wav failed\n");
        }
    }

    void stop() override {
        // 结束正在播放的 aplay 进程（pkill 找不到进程时返回非 0，属预期）
        std::string killCmd = "pkill -f 'aplay .*" + m_audioDir + "' >/dev/null 2>&1 || true";
        int rc = system(killCmd.c_str());
        (void)rc;  // 无残留进程/命令不可用：静默忽略
    }

private:
    // 设备串形如 plughw:C,D → 用 aplay -l 检查 "card C:" 是否存在。
    // 其它形式（default/sysdefault/...）一律认为可用，交由 aplay 自己报错。
    bool cardPresent() const {
        int c = -1, d = -1;
        if (std::sscanf(m_device.c_str(), "plughw:%d,%d", &c, &d) != 2) return true;
        char cmd[160];
        std::snprintf(cmd, sizeof(cmd),
                      "aplay -l 2>/dev/null | grep -q -e '^card %d:'", c);
        return system(cmd) == 0;
    }

    std::string warningPath() const { return m_audioDir + "/warning.wav"; }
    std::string dangerPath() const  { return m_audioDir + "/danger.wav"; }

    // 组装后台播放命令：aplay -q -D <dev> <wav> &
    std::string cmd(const char* file) const {
        return "aplay -q -D " + m_device + " " + m_audioDir + "/" + file + " &";
    }

    std::string m_device;
    std::string m_audioDir;
    bool        m_ok;
};

// -----------------------------------------------------------------------------
// 工厂函数：创建板端语音后端（供 main.cc 直接使用）
// -----------------------------------------------------------------------------
VoiceBackend* createAplayVoiceBackend(const char* device, const char* audioDir) {
    return new AplayVoiceBackend(device, audioDir);
}

}  // namespace hw