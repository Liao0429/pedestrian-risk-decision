# 复杂网络/谱特征/聚类/滤波算法审查报告（Agent 3）

审查对象：`graph_laplacian.{h,py}`、`risk_fsm.{h,py}`、`crowd_dbscan.{h,py}`、`ema_smoother.h`、`kalman_filter.h` 及 `enhanced_decision` 集成。核对结论：C++/Python 谱特征数学定义一致（L=D-A、λ₂、谱半径、特征熵均自然对数、负特征值截断 0、建边严格小于），主要问题在可解释性、收敛性与功能冗余。

## 1. Fiedler 值随 n 缩放，跨帧不可比
【证据】实测 1.5m 等距直线 P5/P10/P20/P30：λ₂=0.382/0.098/0.025/0.011；K4/K8：4.0/8.0（graph_laplacian.py:182、h:307）。相同空间密度仅因人数翻倍 λ₂ 缩 4 倍。
【严重度】高。
【影响】人群规模变化时 λ₂ 无法与阈值比较，论文若直接使用会得出"人多反而不聚集"的错误结论。
【方案】改用归一化拉普拉斯 μ₂（L_sym=I−D^{−1/2}AD^{−1/2}，孤立点对角按 0 处理），值域 [0,2]，跨帧同尺度。成本：与 Jacobi 同阶。验证：P5/K4/随机图与 numpy eigvalsh 比对 μ₂。

## 2. 图不连通时 λ₂≡0，与完全分散不可区分
【证据】实测两簇 K4 相距 5m：density=0.43、clustering=1.0、r_gcc=0.5，但 λ₂=0，与 4 点全分散相同。
【严重度】高。
【影响】两堆密集人群是典型高风险场景，谱特征却判为"不连通=不聚集"；且 λ₂>0 ⟺ r_gcc=1，与既有特征信息重复。
【方案】谱值仅保留定性语义（连通性），决策输入以 r_gcc + DBSCAN 最大簇占比代替。成本：可删除 Jacobi。

## 3. Jacobi 在 max_sweeps=100 内不收敛，谱特征静默回退 0
【证据】按 graph_laplacian.h:155-218 同算法模拟：P8 需 95 次、P10 需 161 次才达 tol=1e-12；P12/P16 200 次仍不收敛（残差 2.2e-6/8.6e-3）。超限即 converged=false，fiedler/谱半径/特征熵全部置 0（h:300-305）。
【严重度】高。
【影响】排队/稀疏长链（n≥10）即触发，板端静默输出 0；现有测试仅覆盖 P5（35 次收敛），未暴露。
【方案】弃用全谱 Jacobi：λ_max 用幂迭代（每次 O(m)，约 20 次收敛）；λ₂ 用局部密度替代。若保留则 max_sweeps≥500 或 tol 放宽至 1e-8，并补 n=64 直线/随机点测试。验证：与 numpy 比对特征值残差。

## 4. DBSCAN 与图建边阈值不一致，min_samples=2 使其退化为连通分量
【证据】DBSCAN 默认 eps=1.5 闭区间（crowd_dbscan.h:119），图建边 edge_dist=2.0 严格小于。实测两点相距 1.8m：图密度=1.0，DBSCAN 判 0 簇、2 噪声。min_samples=2 时簇≡eps 图连通分量，与 r_gcc 重复。
【严重度】中。
【影响】两模块"邻近"语义矛盾，特征无法融合；聚类结果不提供增量信息。
【方案】统一半径（建议 eps=edge_dist=2.0 或均 1.5m 并注明语义）；min_samples 提至 3~4，使 DBSCAN 刻画"局部密度核心"。成本：O(n²) 不变（n≤64）。验证：构造三点两两 1.8m 的簇检查核心点/边界点判定。

## 5. Python/C++ DBSCAN 噪声标签相反
【证据】Python：UNCLASSIFIED=-1、NOISE=-2（crowd_dbscan.py:34-35），实测噪声返回 -2；C++：NOISE=-1、UNCLASSIFIED=-2（crowd_dbscan.h:107-108），头文件自称"与 Python 对应"。
【严重度】中。
【影响】跨语言黄金回归无法直接比对 labels，板端结果与 Python 不一致。
【方案】统一为 sklearn 约定 noise=-1（或 C++ 对齐 Python），并在黄金测试加标签断言。成本：改一行常量。验证：分散点集输出全噪声且标签值两端一致。

## 6. Kalman+EMA+FSM 三级时序滤波重复，延迟叠加
【证据】流水线已有 Kalman（位置级）+ FSM 确认帧 3/5、滞回、DANGER 保持 10 帧/1000ms（risk_fsm.h:65-69）；ema_smoother.h 未接入任何运行路径（仅黄金测试），docs 9.8 仍建议加 EMA。
【严重度】中。
【影响】若按 docs 加 EMA(α=0.3)，10Hz 下时间常数≈3.3 帧，叠加 confirm_up=3，DANGER 触发延迟约 0.6~0.7s；Kalman 已平滑脚点，特征级 EMA 增益有限。
【方案】三选二：保留 Kalman+FSM，放弃 EMA；若保留 EMA，则 confirm_up 降为 1~2、danger_hold_frames 置 0。验证：抖动序列对比两级/三级延迟与误报。

## 7. 新旧双决策系统阈值冲突
【证据】legacy 规则：n≥10 且(rho≥0.55 或 gcc≥0.8)→DANGER（risk_engine.cc:106-107）；FSM score=0.4·norm(n)+0.3·rho+0.3·gcc、up_danger=0.65。实测 n=10、rho=0.6、gcc=0.5：legacy→DANGER，FSM score=0.53→WARNING。fallback 开启时双系统并存且重复 computeFeatures。
【严重度】中。
【影响】报警可能逐帧翻转，浪费 RV1106 算力。
【方案】保持 fallback=false，明确 FSM 为唯一决策源；legacy 仅作论文对照，不参与输出。成本：零。

## 8. 帧间隔/滤波参数配置不一致
【证据】default_config.json frame_dt=0.1（10Hz），kalman_filter.h 默认 1/30；kalman_process_noise 等无 JSON 键，硬编码于 EnhancedConfig（enhanced_decision.h:82-90）。
【严重度】低。
【影响】漏配 frame_dt 时 CV 模型按 30Hz 外推，位置偏差放大，间接污染图特征与 FSM 输入。
【方案】JSON 增加 kalman_* 键，启动时打印生效 dt。验证：固定帧率视频对比两种 dt 下 r_gcc 抖动。

**总体建议**：板端最终方案＝Kalman（位置）+ 共享距离矩阵的 density/r_gcc/clustering + DBSCAN(min_samples≥3) 局部密度特征 + FSM（去 EMA）。谱特征若为论文保留，改用归一化 μ₂ 且仅 n≤32、1~5Hz 低频计算，不作为实时决策输入。