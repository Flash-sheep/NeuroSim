#ifndef ATTENTION_SOFTMAX_H
#define ATTENTION_SOFTMAX_H

class AttentionSoftmax {
public:
    /**
     * @brief 构造函数（纯估算模式）
     * @param _techNode 制程节点 (nm, e.g., 7, 28)
     * @param _clkFreq 主频 (Hz, e.g., 1e9 = 1GHz)
     * @param _bitWidth 数据位宽 (8/16)
     * @param _seqLen 序列长度 (e.g., 2048)
     * @param _numHeads 注意力头数 (e.g., 12)
     * @param _parallelism 并行处理单元数 (e.g., 64)
     */
    AttentionSoftmax(int _techNode, double _clkFreq, int _bitWidth, 
                    int _seqLen, int _numHeads, int _parallelism = 64);

    /// 估算硬件指标
    double GetArea() const;    // 返回面积 (m²)
    double GetPower() const;   // 返回动态功耗 (W)
    double GetLatency() const; // 返回延迟 (s)
    double GetEnergy() const { return GetPower() * GetLatency(); } // 返回能量 (J)

private:
    // --- 配置参数 ---
    int techNode;       // 制程节点 (nm)
    double clkFreq;     // 主频 (Hz)
    int bitWidth;       // 数据位宽
    int seqLen;         // 序列长度
    int numHeads;       // 注意力头数
    int parallelism;    // 并行度

    // --- 预计算指标 ---
    double area;        // 面积 (m²)
    double power;       // 动态功耗 (W)
    double latency;     // 延迟 (s)
};

#endif // ATTENTION_SOFTMAX_H