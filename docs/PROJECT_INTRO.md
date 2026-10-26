# 题目介绍与说明

> 题目：《基于 C++ 的边缘端轻量化行人感知与风险决策软件系统》

## 一、题目介绍

**需求**：公共场所人群过度聚集可能引发踩踏、拥挤等安全事故。传统视频监控依赖人工盯屏，存在实时性差、漏报率高、人工成本大等问题；而高精度深度学习模型通常依赖 GPU 服务器，难以部署到低算力、低成本、无 GPU 的边缘嵌入式设备。因此，需要一套**能在边缘端实时运行、自动检测人群聚集并分级预警**的轻量化软件系统。

**解决的需求**：在低算力边缘设备（Luckfox Pico Max，RV1106，1 TOPS NPU、256MB 内存）上实现实时目标检测与聚集风险分级预警，替代人工盯屏，自动输出 **SAFE / WARNING / DANGER** 三级风险；支持离线运行、低内存低功耗、可扩展。

**解决方法**：
1. **前端轻量化感知**：采用 YOLOv5n 目标检测模型 + INT8 量化，部署到边缘端 NPU 上实时推理，并用 C++/RKNN 实现。
2. **几何空间校正**：采用逆透视映射（IPM），把图像像素脚点映射到地面米制坐标，消除透视畸变导致的距离失真。
3. **图论拓扑分析**：构建行人邻接图（以地面欧氏距离阈值建边），计算图拉普拉斯谱特征（Fiedler 值、谱半径、代数连通度），并结合 DBSCAN 聚类量化人群聚集程度。
4. **时序风险决策**：采用 Kalman/ByteTrack 多目标跟踪平滑脚点，结合 EMA 滤波，并用有限状态机（FSM）做滞回去抖，输出稳定的三级风险等级。
5. **工程化与可配置**：核心算法用 C++11 实现（可交叉编译到 ARM），阈值/目标类别通过 JSON 运行时配置；RTSP 推流 + 浏览器网页实时观看；支持多目标类别（人/车辆/狗等）切换。

## 二、完成题目的基本要求

### 1. 系统功能要求
- **实时检测**：在开发板上实时检测行人/目标并绘制检测框；
- **风险分级**：输出 SAFE / WARNING / DANGER 三级风险并叠加到画面；
- **轻量化**：模型 INT8 量化，内存占用小（实测约 12.5MB），CPU 占用可控，支持离线运行；
- **可视化**：RTSP 推流，浏览器直接观看；画面显示风险等级条；
- **可配置**：阈值、目标类别、IPM 标定通过配置文件运行时调整；
- **可扩展**：支持多目标类别切换、硬件扩展（云台/语音/触摸屏）。

### 2. 算法与实现要求
- 核心算法需理解原理，并体现**组合创新**（IPM 几何校正 + 图拉普拉斯谱 + FSM 时序决策 + Kalman 跟踪）；
- C++ 实现板端部署，Python 原型验证；
- 提供单元测试、真实实验数据与消融实验。

### 3. 实验与文档要求
- 真实数据集实验（图片基线 + 视频增强 + 真机验证），数据可复现；
- 消融实验（各组件贡献）与前后对比（含代价/下降方面）；
- 撰写毕业论文（含硬件章节），答辩演示（网页观看 + 效果视频）。

## 三、参考资料

### 1. 目标检测与轻量化部署
- Ultralytics YOLOv5 / YOLOv8：目标检测模型与训练；
- Rockchip RKNN-Toolkit2：模型 INT8 量化与 NPU 部署；
- COCO 数据集：目标检测训练与评测标准。

### 2. 几何校正
- 逆透视映射（IPM）相关文献（鸟瞰视角 / 车道线检测应用）。

### 3. 图论与复杂网络分析
- 图拉普拉斯矩阵与谱图理论（Fiedler 值、代数连通度）；
- DBSCAN 聚类（Ester et al., 1996）。

### 4. 多目标跟踪与时序决策
- Kalman 滤波（S. Haykin 等）；
- ByteTrack（Zhang et al., 2022）；
- 有限状态机（FSM）在决策与控制中的应用。

### 5. 视频与数据集
- Mixkit（免费视频素材库，用于人群视频实验）；
- COCO 2017（图片实验）。

### 6. 嵌入式平台与工具
- Luckfox Pico 官方文档与 SDK（RV1106）；
- OpenCV（图像处理）；
- CMake / ARM 交叉编译工具链。

## 四、参考文献（用户真实文献 [1]-[23] + 补充核实 [24]-[31]）

### 用户提供（[1]-[23]，已核实格式）
[1] 谢英红,周育竹,韩晓微,等. 基于改进YOLOv5的行人目标检测方法[J]. 沈阳大学学报(自然科学版),2024,36(3):205-212.
[2] 胡倩,皮建勇,胡伟超,等. 基于改进YOLOv5的密集行人检测算法[J]. 计算机工程,2025,51(3):216-228.
[3] 宋子昂,刘惠临. 基于多头自注意力机制和PANet的优化YOLOv5行人检测算法[J]. 宁夏师范学院学报,2024,45(1):93-101.
[4] 杨旭睿,冯宇平,李悦,等. 基于注意力机制改进YOLO-V5的多尺度行人目标检测[J]. 青岛科技大学学报(自然科学版),2024,45(5):127-134.
[5] 曹选,郝万君. 改进YOLO V5的密集行人检测算法研究[J]. 苏州科技大学学报(自然科学版),2022,39(4):64-72.
[6] 杨睿宁,惠飞,金鑫,等. 改进YOLOv5s的复杂交通场景路侧目标检测算法[J]. 计算机工程与应用,2023,59(16):159-169.
[7] 叶心,周斌,马文丽,等. 基于轻量化YOLOv5n算法的交通目标检测研究[J]. 重庆理工大学学报(自然科学),2026,40(1):18-26.
[8] 高德芝,郑榜贵,段建民. 基于逆透视变换的智能车辆定位技术[J]. 计算机测量与控制,2009,17(9):1810-1812.
[9] 陈宁,洪丰,白冰. 基于边缘特征和霍夫变换的斑马线识别方法[J]. 浙江科技学院学报,2019,31(6):476-483.
[10] 吴金闪,狄增如. 从统计物理学看复杂网络研究[J]. 物理学进展,2004,24(1):18-46.
[11] 余鹰,朱慧琳,钱进,等. 基于深度学习的人群计数研究综述[J]. 计算机研究与发展,2021,58(12):2724-2747.
[12] 王陆洋. 基于卷积神经网络的图像人群计数研究[D]. 合肥:中国科学技术大学,2020.
[13] 何宇强. 基于卷积神经网络的人群计数方法研究[D]. 合肥:中国科学技术大学,2022.
[14] 徐芳芯,樊嵘,马小陆. 面向拥挤行人检测的改进YOLOv7算法[J]. 计算机工程,2024,50(3):250-258.
[15] 樊嵘,马小陆. 面向拥挤行人检测的改进DETR算法[J]. 计算机工程与应用,2023,59(19):159-165.
[16] ZHAO L, THORPE C E. Stereo- and neural network-based pedestrian detection[J]. IEEE Transactions on Intelligent Transportation Systems, 2000, 1(3):148-154.
[17] SILVA T C, ZHAO L. Machine learning in complex networks[M]. Cham: Springer, 2016.
[18] SILVA T C, ZHAO L. Stochastic competitive learning in complex networks[J]. IEEE Transactions on Neural Networks and Learning Systems, 2012, 23(3):385-398.
[19] ZHAO L, LAI Y C, PARK K, et al. Onset of traffic congestion in complex networks[J]. Physical Review E, 2005, 71(2):026125.
[20] ZHAO L, CUPERTINO T H, PARK K, et al. Optimal structure of complex networks for minimizing traffic congestion[J]. Chaos, 2007, 17(4):043103.
[21] GAO X, ZHENG Q, VEGA-OLIVEROS D A, et al. Temporal network pattern identification by community modelling[J]. Scientific Reports, 2020, 10(1):240.
[22] URIO P R, VERRI F A N, ZHAO L. Semi-supervised learning by edge domination in complex networks[C]//Proceedings of the 2015 11th International Conference on Natural Computation. Zhangjiajie: IEEE, 2015.
[23] BREVE F, ZHAO L. Particle competition and cooperation in networks for semi-supervised learning with concept drift[C]//2012 IEEE World Congress on Computational Intelligence. Brisbane: IEEE, 2012.

### 补充核实（[24]-[31]，经网络检索确认真实存在）
[24] HOWARD A G, ZHU M, CHEN B, et al. MobileNets: Efficient convolutional neural networks for mobile vision applications[EB/OL]. (2017-04-17). arXiv:1704.04861.
[25] VON LUXBURG U. A tutorial on spectral clustering[J]. Statistics and Computing, 2007, 17(4):395-416.
[26] WOJKE N, BEWLEY A, PAULUS D. Simple online and realtime tracking with a deep association metric[C]//IEEE International Conference on Image Processing (ICIP). Phoenix: IEEE, 2017:3645-3649.
[27] ZHANG Y, SUN P, JIANG Y, et al. ByteTrack: Multi-object tracking by associating every detection box[C]//European Conference on Computer Vision (ECCV). Cham: Springer, 2022:1-21.
[28] SHI W, CAO J, ZHANG Q, et al. Edge computing: Vision and challenges[J]. IEEE Internet of Things Journal, 2016, 3(5):637-646.
[29] ZENG Y, AO X, et al. Lane-line detection based on inverse perspective mapping and Kalman filter[EB/OL]. Semantic Scholar.
[30] AYACHI R, et al. An approach of lane detection based on inverse perspective mapping[C]//IEEE. (车道线 IPM 会议论文, IEEE Xplore 6957662).
[31] GAWANDE U, et al. Pedestrian detection and tracking in video surveillance systems: Issues, challenges and research directions[M/OL]. 2020.

> 说明：补充文献 [24]-[31] 均通过公开学术检索（arXiv、IEEE Xplore、Semantic Scholar、Springer）核实标题与出处真实存在；作者与卷期页码以原文为准，引用前请核对最终版本。
