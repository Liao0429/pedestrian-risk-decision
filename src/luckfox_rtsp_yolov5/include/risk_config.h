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

#include <cmath>
#include <fstream>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "detect_thresholds.h"
#include "../../algorithms/minijson.h"
#include "../../algorithms/risk_fsm.h"

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
    bool     enable_risk_engine_fallback = false;  // 增强决策默认启用时，是否仍跑 RiskEngine 旁路（重复 computeFeatures）
    std::string homography_file;  // 外部 IPM 单应标定文件路径（文本 3x3，与 ipm_calibrate.py 的 .txt/.csv 兼容）。空串=用默认硬编码单应（相机位姿变化时配置此字段即可，无需重编译）
    std::vector<int> target_classes{0};  // 检测目标类别集合（COCO id），默认 [0]=person；可切换 [2,5,7,3]=车辆等
    std::map<int,float> class_conf_thresholds;  // 按类别差异化置信度阈值（COCO id -> 最低置信度 0~1）。空=不区分（仅用全局 BOX_THRESH）
    RiskFSMConfig fsm;                  // 增强决策 FSM 参数（默认与 Python 端 RiskFSMConfig 一致，可 JSON 配置）
    double frame_dt = 1.0 / 30.0;       // Kalman 平滑的帧间隔（秒）；默认 30FPS，板端实际帧率低时应按实测配置
    // 检测阈值（RKNN 后处理用）。旧实现是编译期常量 BOX_THRESH，JSON 里的
    // conf_threshold 没有任何解析分支 -> 现场调参完全无效。
    float  conf_threshold = kDefaultDetectConf;  // 检测置信度阈值 (0,1)，见 detect_thresholds.h
    float  nms_threshold  = kDefaultDetectNms;   // NMS IoU 阈值 (0,1)
    // 轨迹级计数去重开关（task-11）：true=用 ByteTrack+TrackCounter 的身份级人数
    // 替换"当帧检测框数"；false=旧口径（A/B 对照用）。编译期 BOARD_TRACK_COUNT=0 可整体关掉。
    bool   track_count_enabled = true;
    // 谱特征(Jacobi)降频间隔帧数：1=每帧都算；3=每 3 帧算 1 次（默认）
    int    spectral_interval = 3;
  // 触摸画框 UI 运行时开关（编译开关 BOARD_TOUCH_ROI 默认 0）。
  // 只有编译时打开 BOARD_TOUCH_ROI=1 且此处为 true，才会初始化 ILI9341/XPT2046；
  // 默认 false = 完全不碰硬件，行为与不接 UI 的版本一致。
  bool   touch_roi_enabled = false;

    // ROI 危险区域（YOLOv5 像素坐标系，720x480）
    struct RoiZone {
        bool enabled = false;
        std::string name;
        std::vector<std::pair<int,int>> polygon;  // 顶点像素坐标
        int color_r = 0, color_g = 0, color_b = 255;  // BGR
        int thickness = 2;
    } roi;

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

    if (const minijson::Value* arr = root.find("target_classes")) {
        if (!arr->isArray() || arr->size() == 0) {
            throw std::runtime_error("target_classes 必须是非空数组");
        }
        cfg.target_classes.clear();
        for (size_t i = 0; i < arr->size(); ++i) {
            const minijson::Value& e = (*arr)[i];
            if (!e.isNumber() || e.asDouble() < 0.0 || e.asDouble() > 79.0) {
                throw std::runtime_error("target_classes 元素必须是 0~79 整数（COCO 类别 id）");
            }
            cfg.target_classes.push_back(static_cast<int>(e.asDouble()));
        }
    } else if (const minijson::Value* v = root.find("target_class")) {
        // 兼容旧的单个 target_class 配置
        if (!v->isNumber() || v->asDouble() < 0.0 || v->asDouble() > 79.0) {
            throw std::runtime_error("target_class 必须是非负整数（0~79，COCO 类别 id）");
        }
        cfg.target_classes = {static_cast<int>(v->asDouble())};
    }

    if (const minijson::Value* v = root.find("enable_risk_engine_fallback")) {
        if (!v->isBool()) {
            throw std::runtime_error("enable_risk_engine_fallback 必须是布尔值");
        }
        cfg.enable_risk_engine_fallback = v->asBool();
    }

    // 外部 IPM 单应标定文件（可选）。空字符串=未配置（用默认硬编码单应）。
    if (const minijson::Value* v = root.find("homography_file")) {
        if (!v->isString()) {
            throw std::runtime_error("homography_file 必须是字符串（外部单应标定文件路径，空串=不用）");
        }
        cfg.homography_file = v->asString();
    }

    // 按类别差异化置信度阈值（可选）：{"class_conf_thresholds": {"0":0.50, "2":0.60}}
    // 键=COCO id(0~79)，值=该类最低置信度(0~1)。未配置的类别不额外过滤（仍用全局检测阈值）。
    if (const minijson::Value* clsObj = root.find("class_conf_thresholds")) {
        if (!clsObj->isObject()) throw std::runtime_error("class_conf_thresholds 必须是对象");
        cfg.class_conf_thresholds.clear();
        const minijson::Object& entries = clsObj->asObject();
        for (minijson::Object::const_iterator it = entries.begin(); it != entries.end(); ++it) {
            const std::string& keyStr = it->first;
            const minijson::Value& val = it->second;
            char* end = nullptr;
            long cls = std::strtol(keyStr.c_str(), &end, 10);
            if (end == keyStr.c_str() || *end != '\0') {
                throw std::runtime_error("class_conf_thresholds 的键必须是 COCO id（0~79）: \"" + keyStr + "\"");
            }
            if (cls < 0 || cls > 79) {
                throw std::runtime_error("class_conf_thresholds 的键必须是 0~79 整数（COCO id）: \"" + keyStr + "\"");
            }
            if (!val.isNumber() || val.asDouble() < 0.0 || val.asDouble() > 1.0) {
                throw std::runtime_error("class_conf_thresholds 的值必须在 [0,1]: \"" + keyStr + "\"");
            }
            cfg.class_conf_thresholds[static_cast<int>(cls)] = static_cast<float>(val.asDouble());
        }
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

    // 检测置信度阈值 / NMS 阈值（可选，0~1）。缺失时沿用编译期默认值。
    if (const minijson::Value* v = root.find("conf_threshold")) {
        if (!v->isNumber() || v->asDouble() <= 0.0 || v->asDouble() >= 1.0)
            throw std::runtime_error("conf_threshold 必须在 (0,1) 之间");
        cfg.conf_threshold = static_cast<float>(v->asDouble());
    }
    if (const minijson::Value* v = root.find("nms_threshold")) {
        if (!v->isNumber() || v->asDouble() <= 0.0 || v->asDouble() >= 1.0)
            throw std::runtime_error("nms_threshold 必须在 (0,1) 之间");
        cfg.nms_threshold = static_cast<float>(v->asDouble());
    }
    // 轨迹计数去重开关（可选）。缺失默认 true（新口径）。
    if (const minijson::Value* v = root.find("touch_roi_enabled")) {
    if (!v->isBool())
      throw std::runtime_error("touch_roi_enabled 必须是布尔值");
    cfg.touch_roi_enabled = v->asBool();
  }

  if (const minijson::Value* v = root.find("track_count_enabled")) {
        if (!v->isBool())
            throw std::runtime_error("track_count_enabled 必须是布尔值");
        cfg.track_count_enabled = v->asBool();
    }
    // 谱特征降频间隔（可选，1~60 帧）。缺失用默认 3。
    if (const minijson::Value* v = root.find("spectral_interval")) {
        if (!v->isNumber() || v->asDouble() < 1.0 || v->asDouble() > 60.0)
            throw std::runtime_error("spectral_interval 必须是 1~60 的整数（帧）");
        cfg.spectral_interval = static_cast<int>(v->asDouble());
    }

    // 增强决策 Kalman 帧间隔（可选，秒）。缺失用默认 1/30（30FPS）。
    if (const minijson::Value* v = root.find("frame_dt")) {
        if (!v->isNumber() || v->asDouble() <= 0.0)
            throw std::runtime_error("frame_dt 必须为正数（秒，如 0.1 表示 10FPS）");
        cfg.frame_dt = v->asDouble();
    }

    // 增强决策 FSM 参数（可选）。缺失时用默认 RiskFSMConfig（与 Python 端一致）。
    if (const minijson::Value* fsmObj = root.find("fsm")) {
        if (!fsmObj->isObject()) throw std::runtime_error("fsm 必须是对象");
        RiskFSMConfig f;
        if (const minijson::Value* v = fsmObj->find("w1")) {
            if (!v->isNumber() || v->asDouble() < 0.0) throw std::runtime_error("fsm.w1 必须是非负数");
            f.w1 = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("w2")) {
            if (!v->isNumber() || v->asDouble() < 0.0) throw std::runtime_error("fsm.w2 必须是非负数");
            f.w2 = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("w3")) {
            if (!v->isNumber() || v->asDouble() < 0.0) throw std::runtime_error("fsm.w3 必须是非负数");
            f.w3 = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("w4")) {
            if (!v->isNumber() || v->asDouble() < 0.0 || v->asDouble() > 1.0)
                throw std::runtime_error("fsm.w4 必须在 [0,1]");
            f.w4 = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("n_ref")) {
            if (!v->isNumber() || v->asDouble() <= 0.0) throw std::runtime_error("fsm.n_ref 必须为正数");
            f.n_ref = static_cast<int>(v->asDouble());
        }
        if (const minijson::Value* v = fsmObj->find("up_warning")) {
            if (!v->isNumber() || v->asDouble() < 0.0 || v->asDouble() > 1.0)
                throw std::runtime_error("fsm.up_warning 必须在 [0,1]");
            f.up_warning = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("up_danger")) {
            if (!v->isNumber() || v->asDouble() < 0.0 || v->asDouble() > 1.0)
                throw std::runtime_error("fsm.up_danger 必须在 [0,1]");
            f.up_danger = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("down_safe")) {
            if (!v->isNumber() || v->asDouble() < 0.0 || v->asDouble() > 1.0)
                throw std::runtime_error("fsm.down_safe 必须在 [0,1]");
            f.down_safe = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("down_warning")) {
            if (!v->isNumber() || v->asDouble() < 0.0 || v->asDouble() > 1.0)
                throw std::runtime_error("fsm.down_warning 必须在 [0,1]");
            f.down_warning = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("confirm_up")) {
            if (!v->isNumber() || v->asDouble() < 1.0) throw std::runtime_error("fsm.confirm_up 必须 >= 1");
            f.confirm_up = static_cast<int>(v->asDouble());
        }
        if (const minijson::Value* v = fsmObj->find("confirm_down")) {
            if (!v->isNumber() || v->asDouble() < 1.0) throw std::runtime_error("fsm.confirm_down 必须 >= 1");
            f.confirm_down = static_cast<int>(v->asDouble());
        }
        if (const minijson::Value* v = fsmObj->find("danger_hold_frames")) {
            if (!v->isNumber() || v->asDouble() < 0.0) throw std::runtime_error("fsm.danger_hold_frames 必须 >= 0");
            f.danger_hold_frames = static_cast<int>(v->asDouble());
        }
        if (const minijson::Value* v = fsmObj->find("danger_hold_ms")) {
            if (!v->isNumber() || v->asDouble() < 0.0) throw std::runtime_error("fsm.danger_hold_ms 必须 >= 0");
            f.danger_hold_ms = static_cast<int>(v->asDouble());
        }
        if (const minijson::Value* v = fsmObj->find("trend_bias")) {
            if (!v->isNumber() || !std::isfinite(v->asDouble())) throw std::runtime_error("fsm.trend_bias 必须是有限数");
            f.trend_bias = v->asDouble();
        }
        if (const minijson::Value* v = fsmObj->find("min_danger_n")) {
            if (!v->isNumber() || v->asDouble() < 2.0) throw std::runtime_error("fsm.min_danger_n 必须 >= 2");
            f.min_danger_n = static_cast<int>(v->asDouble());
        }
        // 滞回一致性等语义校验（与 Python 端 RiskFSMConfig.__post_init__ 一致）
        std::string err;
        if (!f.validate(err)) {
            throw std::runtime_error("fsm 配置非法: " + err);
        }
        cfg.fsm = f;
    }

    // 语义校验：WARNING 阈值不得高于 DANGER（否则 DANGER 永远无法单独触发）
    // ROI 危险区域
    if (const minijson::Value* roiObj = root.find("roi_zone")) {
        if (!roiObj->isObject()) throw std::runtime_error("roi_zone 必须是对象");
        const minijson::Value* en = roiObj->find("enabled");
        if (en && en->isBool()) cfg.roi.enabled = en->asBool();
        const minijson::Value* nm = roiObj->find("name");
        if (nm && nm->isString()) cfg.roi.name = nm->asString();
        const minijson::Value* poly = roiObj->find("polygon");
        if (poly && poly->isArray()) {
            cfg.roi.polygon.clear();
            for (size_t i = 0; i < poly->size(); ++i) {
                const minijson::Value& pt = (*poly)[i];
                if (!pt.isArray() || pt.size() < 2) throw std::runtime_error("roi_zone.polygon 元素必须是 [x,y] 数组");
                int x = static_cast<int>((*poly)[i][0].asDouble());
                int y = static_cast<int>((*poly)[i][1].asDouble());
                cfg.roi.polygon.push_back(std::make_pair(x, y));
            }
        }
        const minijson::Value* col = roiObj->find("color");
        if (col && col->isArray() && col->size() >= 3) {
            cfg.roi.color_b = static_cast<int>((*col)[0].asDouble());
            cfg.roi.color_g = static_cast<int>((*col)[1].asDouble());
            cfg.roi.color_r = static_cast<int>((*col)[2].asDouble());
        }
        const minijson::Value* th = roiObj->find("thickness");
        if (th && th->isNumber()) cfg.roi.thickness = static_cast<int>(th->asDouble());
    }

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