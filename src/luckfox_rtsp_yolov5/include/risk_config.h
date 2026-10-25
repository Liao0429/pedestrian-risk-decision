#ifndef RISK_CONFIG_H
#define RISK_CONFIG_H

// =============================================================================
// risk_config.h — 风险决策运行时配置（C++11，仅标准库 + minijson）
// -----------------------------------------------------------------------------
// 解决审计指出的问题：risk_engine.cc 原先使用编译期宏（#define）固化阈值，
// 与论文“阈值可配置”承诺不符，也无法在现场/真机快速调参。
//
// 本头文件提供：
//   - RiskConfig：与 config/default_config.json 结构一致的风险阈值配置
//   - loadRiskConfig(path)：从 JSON 文件读取配置并做合法性校验，失败抛
//     std::runtime_error（含具体原因）。文件不存在时也可调用
//     loadRiskConfigOr(path, fallback) 自动回退到默认值。
//
// 用法示例（main.cc 启动阶段调用一次）：
//   #include "risk_config.h"
//   try {
//       RiskConfig cfg = loadRiskConfig("./config/default_config.json");
//       riskEngine.setConfig(cfg);
//   } catch (const std::exception& e) {
//       fprintf(stderr, "加载风险配置失败，使用默认值: %s\n", e.what());
//   }
//
// 默认值与 Python 端 crowd_test.py 的 load_config 保持一致（DANGER 10/0.55/0.80、
// WARNING 5/0.28/0.55、edge_dist 2.0m）。
// =============================================================================

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "../../algorithms/minijson.h"

namespace pedestrian {

// 一组等级阈值：n=人数，rho=网络密度，gcc=最大连通分量占比
struct RiskRule {
    double n;
    double rho;
    double gcc;

    // 默认构造使用 DANGER 默认值；WARNING 通过 {5.0, 0.28, 0.55} 显式指定
    RiskRule(double n_ = 10.0, double rho_ = 0.55, double gcc_ = 0.80)
        : n(n_), rho(rho_), gcc(gcc_) {}
};

// 完整风险决策配置
struct RiskConfig {
    double   edge_dist_m = 2.0;
    RiskRule danger;                // DANGER 阈值（默认 10 / 0.55 / 0.80）
    RiskRule warning{5.0, 0.28, 0.55};  // WARNING 阈值（默认 5 / 0.28 / 0.55）

    void setWarning(double n, double rho, double gcc) {
        warning.n = n; warning.rho = rho; warning.gcc = gcc;
    }
    void setDanger(double n, double rho, double gcc) {
        danger.n = n; danger.rho = rho; danger.gcc = gcc;
    }
};

// 从 JSON 文件加载配置。文件不存在或解析/校验失败抛 std::runtime_error。
inline RiskConfig loadRiskConfig(const std::string& path) {
    std::ifstream ifs(path.c_str());
    if (!ifs.is_open()) {
        throw std::runtime_error("无法打开配置文件: " + path);
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    ifs.close();

    minijson::Value root;
    try {
        root = minijson::parse(ss.str());
    } catch (const minijson::JsonError& e) {
        throw std::runtime_error("配置 JSON 解析失败: " + std::string(e.what()) + " (" + path + ")");
    }
    if (!root.isObject()) {
        throw std::runtime_error("配置根节点必须是对象: " + path);
    }

    RiskConfig cfg;

    if (const minijson::Value* v = root.find("edge_dist_m")) {
        if (!v->isNumber() || v->asDouble() <= 0.0) {
            throw std::runtime_error("edge_dist_m 必须是正数");
        }
        cfg.edge_dist_m = v->asDouble();
    }

    // 读取 danger / warning 子对象（允许部分配置，缺失项用默认值）
    const minijson::Value* dangerObj = root.find("danger");
    if (dangerObj) {
        if (!dangerObj->isObject()) throw std::runtime_error("danger 必须是对象");
        if (const minijson::Value* n = dangerObj->find("n")) {
            if (!n->isNumber() || n->asDouble() < 0.0) throw std::runtime_error("danger.n 必须是非负数");
            cfg.danger.n = n->asDouble();
        }
        if (const minijson::Value* rho = dangerObj->find("rho")) {
            if (!rho->isNumber() || rho->asDouble() < 0.0 || rho->asDouble() > 1.0)
                throw std::runtime_error("danger.rho 必须在 [0,1]");
            cfg.danger.rho = rho->asDouble();
        }
        if (const minijson::Value* gcc = dangerObj->find("gcc")) {
            if (!gcc->isNumber() || gcc->asDouble() < 0.0 || gcc->asDouble() > 1.0)
                throw std::runtime_error("danger.gcc 必须在 [0,1]");
            cfg.danger.gcc = gcc->asDouble();
        }
    }

    const minijson::Value* warnObj = root.find("warning");
    if (warnObj) {
        if (!warnObj->isObject()) throw std::runtime_error("warning 必须是对象");
        if (const minijson::Value* n = warnObj->find("n")) {
            if (!n->isNumber() || n->asDouble() < 0.0) throw std::runtime_error("warning.n 必须是非负数");
            cfg.warning.n = n->asDouble();
        }
        if (const minijson::Value* rho = warnObj->find("rho")) {
            if (!rho->isNumber() || rho->asDouble() < 0.0 || rho->asDouble() > 1.0)
                throw std::runtime_error("warning.rho 必须在 [0,1]");
            cfg.warning.rho = rho->asDouble();
        }
        if (const minijson::Value* gcc = warnObj->find("gcc")) {
            if (!gcc->isNumber() || gcc->asDouble() < 0.0 || gcc->asDouble() > 1.0)
                throw std::runtime_error("warning.gcc 必须在 [0,1]");
            cfg.warning.gcc = gcc->asDouble();
        }
    }

    // 语义校验：WARNING 阈值不得高于 DANGER（否则 DANGER 永远无法单独触发）
    if (cfg.warning.n > cfg.danger.n) {
        throw std::runtime_error("warning.n 不得大于 danger.n（等级语义颠倒）");
    }
    return cfg;
}

// 加载配置；失败时回退到 fallback 并原样返回（不抛异常）。
inline RiskConfig loadRiskConfigOr(const std::string& path, const RiskConfig& fallback) {
    try {
        return loadRiskConfig(path);
    } catch (const std::exception&) {
        return fallback;
    }
}

} // namespace pedestrian

#endif // RISK_CONFIG_H