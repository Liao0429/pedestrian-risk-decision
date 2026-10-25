#ifndef RISK_ENGINE_H
#define RISK_ENGINE_H

#include <vector>
#include <utility>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "risk_config.h"

// 拥挤风险等级
typedef enum {
    CROWD_SAFE = 0,
    CROWD_WARNING = 1,
    CROWD_DANGER = 2
} CrowdLevel;

// 复杂网络特征
typedef struct {
    int    n;         // 节点数(行人数)
    int    m;         // 边数
    double density;   // 网络密度
    double r_gcc;     // 最大连通分量占比
    double clustering;// 平均聚类系数
} RiskFeatures;

// 风险决策引擎：基于复杂网络特征的行人拥挤度/风险判断
class RiskEngine {
public:
    RiskEngine();
    ~RiskEngine();

    // 设置逆透视变换矩阵(像素 -> 地面米)
    void setHomography(const cv::Mat &H);
    // 设置运行时阈值配置（默认值见 risk_config.h，启动时调用一次即可）
    void setConfig(const pedestrian::RiskConfig &cfg);
    const pedestrian::RiskConfig& config() const { return m_cfg; }
    // 清空本帧状态
    void clear();
    // 添加一个行人脚点(像素坐标，需先经 mapCoordinates 还原到原图)
    void addPedestrian(int footX, int footY);
    // 计算网络特征
    void computeFeatures();
    // 规则判断风险等级
    CrowdLevel evaluate();

    const RiskFeatures& features() const { return m_feat; }
    const char* levelName() const { return m_levelName; }
    static const char* levelName(CrowdLevel lvl);

private:
    void mapToGround(float &x, float &y) const;

    cv::Mat m_H;
    std::vector<std::pair<float,float>> m_pts; // 地面坐标(米)
    pedestrian::RiskConfig m_cfg;
    RiskFeatures m_feat;
    CrowdLevel m_level;
    const char* m_levelName;
};

#endif
