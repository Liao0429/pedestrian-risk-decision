// servo.h
// 来自 docs/hardware/ 方案文档，供 Luckfox 开发板硬件扩展集成参考。
// 实际集成时请按板面丝印和 /sys/class/pwm、/dev/ttyS* 实际节点核对。

// servo.h
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

class Servo {
public:
    Servo(int chip, int channel = 0)
        : m_chip(chip), m_chan(channel) {
        snprintf(m_base, sizeof(m_base),
                 "/sys/class/pwm/pwmchip%d/pwm%d", chip, channel);
    }

    // 导出并配置为 50Hz、normal、中位 1.5ms，然后使能
    bool init() {
        char p[64];
        snprintf(p, sizeof(p), "/sys/class/pwm/pwmchip%d/export", m_chip);
        FILE *f = fopen(p, "w");
        if (f) { fprintf(f, "%d", m_chan); fclose(f); usleep(10000); }

        if (!writeFile("period", 20000000)) return false;      // 20ms = 50Hz
        if (!writeFile("polarity", "normal")) return false;
        if (!writeFile("duty_cycle", 1500000)) return false;  // 中位 90°
        if (!writeFile("enable", 1)) return false;
        return true;
    }

    // angle: 0~180
    void setAngle(float angle) {
        if (angle < 0)   angle = 0;
        if (angle > 180) angle = 180;
        long ns = 500000L + (long)((angle / 180.0f) * 2000000.0f);
        writeFile("duty_cycle", ns);
    }

private:
    bool writeFile(const char *attr, long val) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", m_base, attr);
        FILE *f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%ld", val);
        fclose(f);
        return true;
    }
    bool writeFile(const char *attr, const char *val) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", m_base, attr);
        FILE *f = fopen(path, "w");
        if (!f) return false;
        fprintf(f, "%s", val);
        fclose(f);
        return true;
    }

    int  m_chip, m_chan;
    char m_base[128];
};
