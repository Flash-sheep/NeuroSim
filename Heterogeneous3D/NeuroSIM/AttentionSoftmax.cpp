#include "AttentionSoftmax.h"
#include <cmath>
#include <algorithm> // for ceil

AttentionSoftmax::AttentionSoftmax(int _techNode, double _clkFreq, int _bitWidth,
                                  int _seqLen, int _numHeads, int _parallelism)
    : techNode(_techNode), clkFreq(_clkFreq), bitWidth(_bitWidth),
      seqLen(_seqLen), numHeads(_numHeads), parallelism(_parallelism) 
{
    // ===== 面积计算 (m²) =====
    // 基础控制逻辑面积 (mm²)
    double baseArea_mm2 = 0.01 * (techNode / 28.0);
    // 每并行单元面积 (mm²)
    double perUnitArea_mm2 = 0.00002 * (techNode / 28.0);
    // 总面积 (mm²)
    double totalArea_mm2 = baseArea_mm2 + (parallelism * perUnitArea_mm2);
    // 转换为 m² (1 mm² = 10⁻⁶ m²)
    area = totalArea_mm2 * 1e-6;

    // ===== 功耗计算 (W) =====
    // 7nm为基准的每MAC操作能耗 (J)
    double energyPerMac_J = 0.05e-12 * pow(7.0 / techNode, 2.0); // 0.05 pJ → J
    // 总操作次数 = seqLen² × numHeads
    double totalOperations = static_cast<double>(seqLen) * seqLen * numHeads;
    // 动态功耗 = 单次操作能耗 × 操作频率 (W = J/s)
    power = energyPerMac_J * totalOperations * clkFreq;

    // ===== 延迟计算 (s) =====
    // 基础延迟 (ns)
    double baseLatency_ns = 5.0 * (techNode / 7.0);
    // 转换为秒 (1 ns = 10⁻⁹ s)
    double baseLatency_s = baseLatency_ns * 1e-9;
    
    // 每行处理周期数
    double cyclesPerRow = std::ceil(static_cast<double>(seqLen) / parallelism);
    // 分块处理时间 (s)
    double blockTime_s = cyclesPerRow / clkFreq;
    
    latency = baseLatency_s + blockTime_s;
}

// 获取硬件指标
double AttentionSoftmax::GetArea() const { return area; }
double AttentionSoftmax::GetPower() const { return power; }
double AttentionSoftmax::GetLatency() const { return latency; }