/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
//==============================================================================
/**
*/
class FreqWeaveAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    FreqWeaveAudioProcessor();
    ~FreqWeaveAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    
    juce::CriticalSection bufferLock;  // 用于保护缓冲区的线程安全
    bool isSidechainInputActive() const;//检查side chain是否激活
    
    // 用于存储窗函数数据
    std::vector<float> windowingTable;
std::unique_ptr<juce::dsp::WindowingFunction<float>> windowFunction;
    
    //FFT相关数据
    static constexpr int fftOrder = 11; // FFT的阶数，2^11 = 2048点FFT
    static constexpr int fftSize = 1 << fftOrder;
    double sampleRate = 0.0;  // 用于存储采样率
    juce::dsp::FFT fft;
    void processFFTBlock();

    // 准备进行处理的数据缓冲区
    std::vector<float> mainchainFFTData;
    std::vector<float> sidechainFFTData;
    
    // 储存fft后幅度和相位信息
    std::vector<float> mainMagnitude;
    std::vector<float> mainPhase;

    std::vector<float> sidechainMagnitude;
    std::vector<float> sidechainPhase;
    
    // 混合后的 FFT 结果-band1
    std::vector<float> mixedMagnitude1;
    std::vector<float> mixedPhase1;

    // 混合后的 FFT 结果-band2
    std::vector<float> mixedMagnitude2;
    std::vector<float> mixedPhase2;
    // 用于IFFT的buffer
    std::vector<float> outMagnitude;
    std::vector<float> outPhase;
    // 用于存储逆 FFT 的数据
    std::vector<float> outputFFTData;
    //std::vector<float> outputMagnitude;
    std::vector<float> fftFrequency; // 频率向量
    // 公共成员以访问ValueTreeState
    juce::AudioProcessorValueTreeState parameters;
    
private:
    //==============================================================================
    //管理音频格式
    juce::AudioFormatManager formatManager;
    // 流式 STFT 状态（50% 重叠 + Hann 窗 + overlap-add），按声道各一份
    std::vector<std::vector<float>> mainHistory;   // 每个声道最近 fftSize 个主输入样本（环形）
    std::vector<std::vector<float>> sideHistory;   // 每个声道最近 fftSize 个侧链样本（环形）
    std::vector<std::vector<float>> olaBuffer;     // 每个声道的 overlap-add 累加区，长度 fftSize
    std::vector<std::vector<float>> outFifo;       // 每个声道的输出 FIFO（环形）
    std::vector<float> fftScratch;                 // FFT 工作区，JUCE 要求长度必须是 2 * fftSize
    int historyWritePos = 0;                       // history 的写指针，同时也指向最老的样本
    int hopCounter = 0;                            // 距离下一帧还差多少个样本
    int outFifoCapacity = 0;
    int outFifoReadPos = 0;
    int outFifoCount = 0;
    // 保护 FFT 数据的互斥锁
    juce::CriticalSection fftDataLock;
    // 频率向量
    std::vector<float> mainchainFrequency;
    std::vector<float> sidechainFrequency;
    std::vector<float> mixedFrequency;
    
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

//    // FFT 相关成员变量
//    int fftOrder = 11;               // FFT 的阶数（log2大小）
//    int fftSize = 2048;                // FFT 大小（2的幂次方）
    int hopSize = fftSize / 2; // 50% 重叠
    float sampleRateOverFftSize;
    // 辅助方法
    void performFFT(float* inputData, std::vector<float>& magnitude, std::vector<float>& phase, bool isMainchain);
    void crossSynthesis();
    void performIFFT(float* data);      // data 长度必须是 2 * fftSize，结果在前 fftSize 个元素
    void processFrame(int channel, bool useSidechain, bool pushOutput);
    std::mutex vectorMutex;
    void adjustSidechainToStereo(juce::AudioBuffer<float>& buffer, int mainNumChannels);
    juce::NormalisableRange<float> createFrequencyRange();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreqWeaveAudioProcessor)
};


