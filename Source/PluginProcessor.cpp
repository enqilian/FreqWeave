/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AudioFifo.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <JuceHeader.h>
#include <juce_core/juce_core.h>  // 添加这个头文件以使用 jlimit

//==============================================================================
// PluginProcessor.cpp
FreqWeaveAudioProcessor::FreqWeaveAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput ("Input", juce::AudioChannelSet::stereo(), true)       // 主输入
                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)   // 侧链输入，false代表他不是总线，即为辅助总线。
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),//输出
windowFunction(nullptr),    // 初始化输入缓冲区，假设是立体声和2048点FFT
sampleRate(0.0), // 初始化侧链输入缓冲区
fft(fftOrder),    // 初始化输出缓冲区
fftFrequency(fftSize / 2),           // 初始化采样率
sampleRateOverFftSize(0.0f), // 初始化为默认值
parameters (*this, nullptr, juce::Identifier ("Parameters"),
            {
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("cutFrequencyFrom1",1), "CutFrequencyFrom1", createFrequencyRange(), 2000.0f),
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("cutFrequencyFrom2",1), "CutFrequencyFrom2", createFrequencyRange(), 2000.0f),
    //band可以是中心的
    //slider非线性
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("FrequencyBandLength",1), "FrequencyBandLength",  juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f),//length之于cutofffrequency的比率 q值 比例关系
    //0-200%*cutofffrequency
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("ExchangeBandValue",1), "ExchangeBandValueOrNot", 0.0f, 1.0f, 1.0f),
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("band1Mix",1), "Band1Mix", 0.0f, 1.0f, 0.01f),
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("band2Mix",1), "Band2Mix", 0.0f, 1.0f, 0.01f),
}),  // 假设频率数据只需要一半
    formatManager(),
    mainchainFrequency(fftSize / 2),
    sidechainFrequency(fftSize / 2),
    mixedFrequency(fftSize / 2)
{
    // 设置默认总线布局
    BusesLayout defaultLayout;
    defaultLayout.inputBuses.add(juce::AudioChannelSet::stereo());
    defaultLayout.inputBuses.add(juce::AudioChannelSet::stereo()); // 侧链输入
    defaultLayout.outputBuses.add(juce::AudioChannelSet::stereo());

    // 直接将侧链输入总线的通道数设置为 2
    getBus(true, 1)->setNumberOfChannels(2);

    formatManager.registerBasicFormats(); // 注册基本音频格式

    // 初始化FFT数据缓冲区
    mainchainFFTData.resize(fftSize, 0.0f);
    sidechainFFTData.resize(fftSize, 0.0f);

    // 初始化频率、幅度和相位缓冲区
    mainMagnitude.resize(fftSize / 2 + 1);
    mainPhase.resize(fftSize / 2 + 1);

    sidechainMagnitude.resize(fftSize / 2 + 1);
    sidechainPhase.resize(fftSize / 2 + 1);
    
    mixedMagnitude1.resize(fftSize / 2 + 1);
    mixedPhase1.resize(fftSize / 2 + 1);
    
    mixedMagnitude2.resize(fftSize / 2 + 1);
    mixedPhase2.resize(fftSize / 2 + 1);
    outMagnitude.resize(fftSize / 2 + 1);
    outPhase.resize(fftSize / 2 + 1);
    outputFFTData.resize(fftSize, 0.0f); // 逆 FFT 存储 fftSize 个元素
    // 初始化代码
    sampleRateOverFftSize = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
}


FreqWeaveAudioProcessor::~FreqWeaveAudioProcessor()
{
}

//==============================================================================
const juce::String FreqWeaveAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FreqWeaveAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FreqWeaveAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FreqWeaveAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double FreqWeaveAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FreqWeaveAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int FreqWeaveAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FreqWeaveAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String FreqWeaveAudioProcessor::getProgramName (int index)
{
    return {};
}

void FreqWeaveAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
juce::NormalisableRange<float> FreqWeaveAudioProcessor::createFrequencyRange()
{
    // 定义频率范围的最小值和最大值
    float minFreq = 20.0f;       // 最小频率为 20Hz
    float maxFreq = 20000.0f;    // 最大频率为 20000Hz

    // 自定义的从 [0,1] 到实际频率的映射函数
    auto convertFrom0To1 = [](float start, float end, float normalisedValue) -> float
    {
        if (normalisedValue < 0.5f)
        {
            // 低频段（start 到 2000Hz），使用指数映射
            float lowMin = start;             // 低频段的最小值
            float lowMax = 2000.0f;           // 低频段的最大值
            float fraction = normalisedValue / 0.5f;  // 将 normalisedValue 归一化到 [0,1]
            return lowMin * std::pow(lowMax / lowMin, fraction);  // 指数映射
        }
        else
        {
            // 高频段（2000Hz 到 end），使用平方映射
            float highMin = 2000.0f;          // 高频段的最小值
            float highMax = end;              // 高频段的最大值
            float fraction = (normalisedValue - 0.5f) / 0.5f;  // 将 normalisedValue 归一化到 [0,1]
            return highMin + (highMax - highMin) * fraction * fraction;  // 平方映射，加速高频段
        }
    };

    // 自定义的从实际频率到 [0,1] 的逆映射函数
    auto convertTo0To1 = [](float start, float end, float value) -> float
    {
        // 限制输入值在合法范围内
        value = juce::jlimit(start, end, value);

        if (value < 2000.0f)
        {
            // 低频段，使用对数逆映射
            float lowMin = start;             // 低频段的最小值
            float lowMax = 2000.0f;           // 低频段的最大值
            return 0.5f * std::log(value / lowMin) / std::log(lowMax / lowMin);  // 对数逆映射
        }
        else
        {
            // 高频段，使用平方根逆映射
            float highMin = 2000.0f;          // 高频段的最小值
            float highMax = end;              // 高频段的最大值
            float fraction = (value - highMin) / (highMax - highMin);  // 归一化到 [0,1]
            return 0.5f + 0.5f * std::sqrt(fraction);  // 平方根逆映射
        }
    };

    // 定义 snapToLegalValueFunc 函数，用于将值捕捉到合法范围
    auto snapToLegalValueFunc = [](float start, float end, float value) -> float
    {
        // 不对值进行修改，直接返回原值
        return value;
    };

    // 创建 NormalisableRange 对象，包含自定义的映射函数和捕捉函数
    return juce::NormalisableRange<float>(
        minFreq,                   // 起始值 20Hz
        maxFreq,                   // 结束值 20000Hz
        convertFrom0To1,           // 自定义的从 [0,1] 到实际频率的映射函数
        convertTo0To1,             // 自定义的从实际频率到 [0,1] 的逆映射函数
        snapToLegalValueFunc       // 捕捉函数，直接返回原值
    );
    DBG("Create frequency range");
}

void FreqWeaveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 获取输入和输出通道数量
    int mainBusNumInputChannels = getBus(true, 0)->getNumberOfChannels(); // 主输入总线的通道数
    int sidechainBusNumInputChannels = getBus(true, 1)->getNumberOfChannels();  // 默认值
    DBG("sidechainBusNumInputChannels: " << sidechainBusNumInputChannels);

    int numOutputChannels = getTotalNumOutputChannels(); // 输出通道的总数
    DBG("prepareToPlay Called");
    DBG("Sample Rate: " << sampleRate);
    DBG("Samples Per Block: " << samplesPerBlock);
    // 检查 FFT 大小和 FIFO 初始化参数
    jassert(fftSize > 0);
    jassert(mainBusNumInputChannels > 0);
    jassert(getBusCount(true) > 0);
    DBG("FIFOs Initialized");
    fft = juce::dsp::FFT (std::log2 (fftSize)); // 初始化 FFT 处理器，使用 FFT 大小的对数值（以 2 为底）

    // 流式 STFT 的全部状态都在这里重建。宿主每次重新配置总线（比如启用侧链）都会走一遍
    // releaseResources() -> prepareToPlay()，所以这里必须把缓冲区恢复到正确大小并清零，
    // 否则 processBlock() 会访问大小不对的 vector 导致崩溃。
    const int numChannels = juce::jmax (1, getTotalNumOutputChannels());
    const int maxBlock = juce::jmax (samplesPerBlock, fftSize);
    outFifoCapacity = fftSize * 4 + maxBlock * 2; // 远大于任何时刻 FIFO 里可能积压的样本数

    auto resetChannels = [numChannels] (std::vector<std::vector<float>>& state, int length)
    {
        state.assign ((size_t) numChannels, std::vector<float> ((size_t) length, 0.0f));
    };
    resetChannels (mainHistory, fftSize);
    resetChannels (sideHistory, fftSize);
    resetChannels (olaBuffer, fftSize);
    resetChannels (outFifo, outFifoCapacity);
    fftScratch.assign ((size_t) fftSize * 2, 0.0f);

    historyWritePos = 0;
    hopCounter = 0;
    outFifoReadPos = 0;
    outFifoCount = hopSize; // 预填 hopSize 个零，保证输出 FIFO 任何时候都不会被读空

    // 周期 Hann 窗：50% 重叠时相邻两帧的窗口之和恰好等于 1，不做处理时能完美重建
    windowingTable.resize ((size_t) fftSize);
    for (int n = 0; n < fftSize; ++n)
        windowingTable[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) fftSize));

    // 初始化 FFT 相关的缓冲区
    mainchainFFTData.resize(fftSize * 2, 0.0f);
    mainMagnitude.resize (fftSize / 2 + 1, 0.0f);
    mainPhase.resize(fftSize / 2 + 1);
    sidechainFFTData.resize(fftSize * 2, 0.0f);
    sidechainMagnitude.resize (fftSize / 2 + 1, 0.0f);
    sidechainPhase.resize(fftSize / 2 + 1);
    mixedMagnitude1.resize(fftSize / 2 + 1);
    mixedPhase1.resize(fftSize / 2 + 1);
    mixedMagnitude2.resize(fftSize / 2 + 1);
    mixedPhase2.resize(fftSize / 2 + 1);
    outMagnitude.resize(fftSize / 2 + 1);
    outPhase.resize(fftSize / 2 + 1);
    outputFFTData.resize(fftSize * 2, 0.0f);
    // 初始化 windowFunction 窗函数
    windowFunction = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::hann); // 使用 Hann 窗函数初始化

    this->sampleRate = sampleRate; // 存储采样率
    // 预计算值
    sampleRateOverFftSize = static_cast<float> (sampleRate) / static_cast<float> (fftSize); // 计算采样率除以 FFT 大小的值
    
    // 计算频率向量
    mainchainFrequency.resize(fftSize / 2 + 1);
    sidechainFrequency.resize(fftSize / 2 + 1);
    mixedFrequency.resize(fftSize / 2 + 1);

    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        float freq = bin * sampleRateOverFftSize; // 计算每个 bin 的频率值
        mainchainFrequency[bin] = freq; // 存储到主链频率缓冲区
        sidechainFrequency[bin] = freq; // 存储到侧链频率缓冲区
        mixedFrequency[bin] = freq; // 存储到混合频率缓冲区
    }
    // 如果采样率变化，需要重新初始化 FFT 或缓冲区
    jassert(fftSize == (1 << fftOrder));
    DBG("fftSize Changed");
    int inputBusCount = getBusCount(true); // true 表示输入总线
    DBG("Input Bus Count: " << inputBusCount); // 应该输出 2

    // 流式 STFT 的算法延迟恰好是 fftSize 个采样点（一帧的长度）。
    // 这里把延迟报告给宿主，宿主才能对其它没经过这个插件的轨道做延迟补偿（PDC），避免轨道错位。
    setLatencySamples(fftSize);
}

void FreqWeaveAudioProcessor::releaseResources()
{
    // 这里不要清空/释放任何 processBlock() 或编辑器频谱显示会用到的 vector：
    // 编辑器的定时器可能正在另一个线程读取它们，所有状态都由 prepareToPlay() 统一重建。
    windowFunction.reset();
}


bool FreqWeaveAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    DBG("Main Input Channels: " << layouts.getMainInputChannelSet().size());
    DBG("Main Output Channels: " << layouts.getMainOutputChannelSet().size());
    DBG("Input Buses Count: " << layouts.inputBuses.size());
    // 检查主输出是否为立体声
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    {
        DBG("Main output is not stereo");
        return false;
    }

    // 检查主输入是否为立体声
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
    {
        DBG("Main input is not stereo");
        return false;
    }
    int sidechainBusNumInputChannels = getBus(true, 1)->getNumberOfChannels();  // 默认值
    if (layouts.inputBuses.size() > 1)
    {
        auto sidechainInput = layouts.getChannelSet(true, 1);
        DBG("Sidechain Channels: " << sidechainInput.size());
        DBG("sidechainInput: " << sidechainInput.getDescription());

        // 检查侧链输入是否禁用或为单声道/立体声
        if (!sidechainInput.isDisabled())
        {
            if (sidechainInput != juce::AudioChannelSet::mono() &&
                sidechainInput != juce::AudioChannelSet::stereo())
            {
                DBG("Sidechain input is neither mono nor stereo");
                return false;
            }
        }
        else
        {
            DBG("Sidechain input is disabled");
        }
    }

    // 如果所有检查通过，则支持该布局
    DBG("Layout supported: Mono and Stereo");
    return true;
}


//检查sideChain input是否被激活
bool FreqWeaveAudioProcessor::isSidechainInputActive() const
{
    const juce::AudioProcessor::Bus* sidechainBus = getBus(true, 1); // 输入总线索引 1 为侧链
    if (sidechainBus == nullptr || !sidechainBus->isEnabled())
        return false;

    // 侧链单声道、立体声都算激活（跟 isBusesLayoutSupported 的判定保持一致）
    return sidechainBus->getNumberOfChannels() > 0;
}



//FFT操作
void FreqWeaveAudioProcessor::performFFT(float* inputData, std::vector<float>& magnitude, std::vector<float>& phase, bool isMainchain)
{
    // 注意：本函数总是从 processBlock() 内部调用，vectorMutex 在那里已经加过锁，
    // 这里不能再次加锁——std::mutex 不可重入，重复加锁会导致音频线程死锁。
    // 执行 FFT
    fft.performRealOnlyForwardTransform(inputData);

    // 计算幅度和相位
    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        float real = inputData[2 * bin];
        float imag = inputData[2 * bin + 1];
        magnitude[bin] = std::sqrt(real * real + imag * imag);
        phase[bin] = std::atan2(imag, real);
    }
}

void FreqWeaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    std::lock_guard<std::mutex> lock(vectorMutex); // 确保线程安全

    const int numSamples = buffer.getNumSamples();
    const int mainNumChannels = juce::jmin(getBus(true, 0)->getNumberOfChannels(), buffer.getNumChannels());

    // 状态还没有按当前声道数建好（比如宿主还没调用 prepareToPlay），宁可原样放行也不能越界访问
    if (mainNumChannels <= 0 || (int) mainHistory.size() < mainNumChannels || outFifoCapacity <= 0)
        return;

    // 侧链总线已启用，并且宿主真的给了足够的声道数，才使用侧链
    const int sidechainNumChannels = getBus(true, 1)->getNumberOfChannels();
    const bool useSidechain = isSidechainInputActive()
                              && sidechainNumChannels > 0
                              && buffer.getNumChannels() >= mainNumChannels + sidechainNumChannels;

    // 逐样本把输入写进各声道的历史缓冲区，每攒够 hopSize 个新样本就处理一帧（帧长 fftSize，
    // 帧间 50% 重叠）。没有侧链时也走同样的流程，只是不做频段交换，这样不管侧链有没有接上，
    // 插件的延迟始终固定为 fftSize，和 setLatencySamples() 报告的一致。
    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int ch = 0; ch < mainNumChannels; ++ch)
        {
            mainHistory[(size_t) ch][(size_t) historyWritePos] = buffer.getReadPointer(ch)[sample];

            if (useSidechain)
            {
                // 侧链是单声道时，左右声道都用它；是立体声时左对左、右对右
                const int scChannel = mainNumChannels + juce::jmin(ch, sidechainNumChannels - 1);
                sideHistory[(size_t) ch][(size_t) historyWritePos] = buffer.getReadPointer(scChannel)[sample];
            }
        }
        historyWritePos = (historyWritePos + 1) % fftSize;

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;

            const bool pushOutput = (outFifoCount + hopSize <= outFifoCapacity);
            for (int ch = 0; ch < mainNumChannels; ++ch)
                processFrame(ch, useSidechain, pushOutput);

            if (pushOutput)
                outFifoCount += hopSize;
        }
    }

    // 输入已经全部读完，现在把处理好的输出从 FIFO 里取出来写回缓冲区。
    // FIFO 在 prepareToPlay() 里预填了 hopSize 个零，正常情况下不会被读空，
    // 万一不够就补静音，绝不越界。
    const int available = juce::jmin(numSamples, outFifoCount);
    for (int ch = 0; ch < mainNumChannels; ++ch)
    {
        float* out = buffer.getWritePointer(ch);
        const auto& fifo = outFifo[(size_t) ch];
        for (int i = 0; i < numSamples; ++i)
            out[i] = (i < available) ? fifo[(size_t) ((outFifoReadPos + i) % outFifoCapacity)] : 0.0f;
    }
    outFifoReadPos = (outFifoReadPos + available) % outFifoCapacity;
    outFifoCount -= available;
}

void FreqWeaveAudioProcessor::processFrame(int channel, bool useSidechain, bool pushOutput)
{
    // 从历史缓冲区取出最近 fftSize 个样本（historyWritePos 指向最老的那个），乘 Hann 窗，
    // 后半截补零——JUCE 的 FFT 要求工作区长度必须是 2 * fftSize
    auto loadWindowed = [this] (const std::vector<float>& history)
    {
        for (int i = 0; i < fftSize; ++i)
            fftScratch[(size_t) i] = history[(size_t) ((historyWritePos + i) % fftSize)] * windowingTable[(size_t) i];
        std::fill(fftScratch.begin() + fftSize, fftScratch.end(), 0.0f);
    };

    {
        // 频谱数据会被编辑器的定时器在另一个线程里读取，所以修改期间要持有 bufferLock
        const juce::ScopedLock sl(bufferLock);

        loadWindowed(mainHistory[(size_t) channel]);
        performFFT(fftScratch.data(), mainMagnitude, mainPhase, true);

        if (useSidechain)
        {
            loadWindowed(sideHistory[(size_t) channel]);
            performFFT(fftScratch.data(), sidechainMagnitude, sidechainPhase, false);
            crossSynthesis();
        }
        else
        {
            outMagnitude = mainMagnitude;
            outPhase = mainPhase;
        }

        performIFFT(fftScratch.data());
    }

    // overlap-add：逆变换结果累加进 OLA 区，最前面 hopSize 个样本已经和上一帧叠加完毕，可以输出了
    auto& ola = olaBuffer[(size_t) channel];
    for (int i = 0; i < fftSize; ++i)
        ola[(size_t) i] += fftScratch[(size_t) i];

    if (pushOutput)
    {
        auto& fifo = outFifo[(size_t) channel];
        const int writePos = (outFifoReadPos + outFifoCount) % outFifoCapacity;
        for (int i = 0; i < hopSize; ++i)
            fifo[(size_t) ((writePos + i) % outFifoCapacity)] = ola[(size_t) i];
    }

    // OLA 区整体前移 hopSize 个样本，尾部补零
    std::copy(ola.begin() + hopSize, ola.end(), ola.begin());
    std::fill(ola.end() - hopSize, ola.end(), 0.0f);
}
//void FreqWeaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
//{
//    std::lock_guard<std::mutex> lock(vectorMutex); // 确保线程安全
//    auto numSamples = buffer.getNumSamples();
//
//    // 获取主链和侧链的通道数量
//    int mainNumChannels = getBus(true, 0)->getNumberOfChannels();
//    int sidechainNumChannels = getBus(true, 1)->getNumberOfChannels();
//
//    if (isSidechainInputActive()) // 如果侧链激活
//    {
//        // 处理主链的环形缓冲区并将其音量设置为0（静音）
//        for (int channel = 0; channel < mainNumChannels; ++channel)
//        {
//            float* writePtr = buffer.getWritePointer(channel); // 获取主链输出通道数据
//
//            // 将主链的音量设置为 0
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr[sample] = 0.0f;  // 设置为静音
//            }
//        }
//
//        // 处理侧链的环形缓冲区并增大音量
//        for (int channel = 0; channel < sidechainNumChannels; ++channel)
//        {
//            const float* readPtr_1 = buffer.getReadPointer(channel + mainNumChannels);  // 获取侧链通道数据
//            float* writePtr_1 = buffer.getWritePointer(channel + mainNumChannels);      // 获取侧链输出通道数据
//
//            // 对侧链音量进行放大（比如倍增音量）
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr_1[sample] = readPtr_1[sample] * 2.0f;  // 假设增大音量倍数为 2
//            }
//        }
//        DBG("Sidechain activated: Main output (volume * 0), Sidechain output (volume * 2)");
//    }
//    else // 如果侧链未激活
//    {
//        // 直接将所有通道的音量设置为 0，输出静音
//        for (int channel = 0; channel < mainNumChannels; ++channel)
//        {
//            float* writePtr = buffer.getWritePointer(channel); // 获取输出通道数据
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr[sample] = 0.0f;  // 设置为静音
//            }
//        }
//
//        for (int channel = 0; channel < sidechainNumChannels; ++channel)
//        {
//            float* writePtr_1 = buffer.getWritePointer(channel + mainNumChannels); // 获取输出通道数据
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr_1[sample] = 0.0f;  // 设置为静音
//            }
//        }
//        DBG("Sidechain not activated: All output muted (volume * 0)");
//    }
//}

//void FreqWeaveAudioProcessor::crossSynthesis()
//{
//
//    // 获取参数值
//    float cutFrequencyFrom1 = parameters.getParameterAsValue("cutFrequencyFrom1").getValue();
//    float cutFrequencyFrom2 = parameters.getParameterAsValue("cutFrequencyFrom2").getValue();
//    float bandLength = parameters.getParameterAsValue("FrequencyBandLength").getValue();
//    float exchangeBandValue = parameters.getParameterAsValue("ExchangeBandValue").getValue();
//    float band1Mix = parameters.getParameterAsValue("band1Mix").getValue();
//    float band2Mix = parameters.getParameterAsValue("band2Mix").getValue();
//
//    // 计算 bandLength 对应的频段宽度
//    float bandWidth = juce::jmap(bandLength, 0.0f, 2.0f, 20.0f, 2000.0f); // 可以调整映射范围
//    // 确保频段宽度在合理范围内
//    bandWidth = juce::jlimit(20.0f, static_cast<float>(sampleRate) / 2.0f, bandWidth);
//
//    // 确保 cutFrequency 在合理范围内
//    cutFrequencyFrom1 = juce::jlimit(20.0f, static_cast<float>(sampleRate) / 2.0f, cutFrequencyFrom1);
//    cutFrequencyFrom2 = juce::jlimit(20.0f, static_cast<float>(sampleRate) / 2.0f, cutFrequencyFrom2);
//
//    // 计算中心 bin 索引
//    int centerBin1 = static_cast<int>(cutFrequencyFrom1 * fftSize / static_cast<float>(sampleRate));
//    int centerBin2 = static_cast<int>(cutFrequencyFrom2 * fftSize / static_cast<float>(sampleRate));
//
//    // 计算半带宽对应的 bin 数
//    int halfBandBins1 = static_cast<int>((bandWidth / 2.0f) * fftSize / static_cast<float>(sampleRate));
//    int halfBandBins2 = static_cast<int>((bandWidth / 2.0f) * fftSize / static_cast<float>(sampleRate));
//
//    // 确保半带宽至少为 1
//    halfBandBins1 = std::max(1, halfBandBins1);
//    halfBandBins2 = std::max(1, halfBandBins2);
//
//    // 计算索引范围
//    int startBin1 = juce::jmax(0, centerBin1 - halfBandBins1);
//    int endBin1 = juce::jmin(fftSize / 2, centerBin1 + halfBandBins1);
//
//    int startBin2 = juce::jmax(0, centerBin2 - halfBandBins2);
//    int endBin2 = juce::jmin(fftSize / 2, centerBin2 + halfBandBins2);///test2
//
//
//    // 添加断言以确保索引合法
//    jassert(startBin1 >= 0 && startBin1 <= fftSize / 2);
//    jassert(endBin1 >= 0 && endBin1 <= fftSize / 2);
//    jassert(startBin2 >= 0 && startBin2 <= fftSize / 2);
//    jassert(endBin2 >= 0 && endBin2 <= fftSize / 2);
//    DBG("startBinAndEndBinGreat");
//
//    // 初始化混合后的幅度和相位为主链和侧链的原始值
//    mixedMagnitude1 = mainMagnitude;
//    mixedPhase1 = mainPhase;
//    mixedMagnitude2 = sidechainMagnitude;
//    mixedPhase2 = sidechainPhase;
void FreqWeaveAudioProcessor::crossSynthesis()
{
    // 1) 获取参数值（getRawParameterValue 是无锁的原子读取，适合在音频线程里用；
    //    getParameterAsValue 每次都会创建 juce::Value 对象，会在音频线程里做堆分配）
    float cutFrequencyFrom1  = parameters.getRawParameterValue("cutFrequencyFrom1")->load();
    float cutFrequencyFrom2  = parameters.getRawParameterValue("cutFrequencyFrom2")->load();
    float bandLength         = parameters.getRawParameterValue("FrequencyBandLength")->load();
    float exchangeBandValue  = parameters.getRawParameterValue("ExchangeBandValue")->load();
    float band1Mix           = parameters.getRawParameterValue("band1Mix")->load();
    float band2Mix           = parameters.getRawParameterValue("band2Mix")->load();

    // 2) 用倍频程（octave）而不是固定 Hz 来定义带宽——这是主流参数 EQ 的 Q 值本质上
    //    采用的方式：带宽随中心频率等比例缩放，这样同一个 Band Length 设置，
    //    不管中心频率在哪，在对数频谱图上看起来宽度都是一致的。
    //    bandLength 参数本身范围还是 [0, 2]（不改自动化范围），只是含义从
    //    "多少 Hz" 变成 "多少个倍频程"。
    float octaves = juce::jmap(bandLength, 0.0f, 2.0f, 0.1f, 3.0f);
    float halfOctave = octaves * 0.5f;
    float octaveFactor = std::pow(2.0f, halfOctave); // 中心频率 * / octaveFactor = 上/下边界

    // 3) 为了保证上下边界不越界，先算出中心频率的合法范围：
    //    下边界 = centreFreq / octaveFactor >= 20Hz
    //    上边界 = centreFreq * octaveFactor <= Nyquist
    float minCenterFreq = 20.0f * octaveFactor;
    float maxCenterFreq = (sampleRate * 0.5f) / octaveFactor;

    cutFrequencyFrom1 = juce::jlimit(minCenterFreq, maxCenterFreq, cutFrequencyFrom1);
    cutFrequencyFrom2 = juce::jlimit(minCenterFreq, maxCenterFreq, cutFrequencyFrom2);

    // 4) 每个 band 各自根据自己的中心频率算上下边界（倍频程是乘除关系，
    //    不是加减关系，所以两个 band 不能共用一个对称的 halfBandBins）
    float lowerFreq1 = cutFrequencyFrom1 / octaveFactor;
    float upperFreq1 = cutFrequencyFrom1 * octaveFactor;
    float lowerFreq2 = cutFrequencyFrom2 / octaveFactor;
    float upperFreq2 = cutFrequencyFrom2 * octaveFactor;

    // 5) 换算成 bin 索引
    int startBin1 = juce::jmax(0, static_cast<int>(lowerFreq1 * fftSize / sampleRate));
    int endBin1   = juce::jmin(fftSize / 2, static_cast<int>(upperFreq1 * fftSize / sampleRate));
    int startBin2 = juce::jmax(0, static_cast<int>(lowerFreq2 * fftSize / sampleRate));
    int endBin2   = juce::jmin(fftSize / 2, static_cast<int>(upperFreq2 * fftSize / sampleRate));

    // 保证至少有 1 个 bin 宽
    if (endBin1 <= startBin1) endBin1 = juce::jmin(fftSize / 2, startBin1 + 1);
    if (endBin2 <= startBin2) endBin2 = juce::jmin(fftSize / 2, startBin2 + 1);

    // 10) 断言检查，确认不会越界
    jassert(startBin1 >= 0 && startBin1 <= fftSize / 2);
    jassert(endBin1   >= 0 && endBin1   <= fftSize / 2);
    jassert(startBin2 >= 0 && startBin2 <= fftSize / 2);
    jassert(endBin2   >= 0 && endBin2   <= fftSize / 2);

    // 12) 初始化混合后的幅度和相位（band1、band2 都以主输入为基底，互不影响）
    mixedMagnitude1 = mainMagnitude;
    mixedPhase1     = mainPhase;
    mixedMagnitude2 = mainMagnitude;
    mixedPhase2     = mainPhase;

    // exchangeBandValue != 0 时为"硬切换"：band1、band2 各自在自己的频率范围内
    // 完全替换为侧链内容，不再跟另一个 band 互换位置（避免把 band1 的内容搬到 band2 的频率上）；
    // exchangeBandValue == 0 时为"混合"：按各自的 Mix 比例在自己的频率范围内混合主链和侧链
    const bool hardSwap = exchangeBandValue != 0.0f;

    // 在 band1 频段内，独立地对主链和侧链进行交换/混合
    for (int i = startBin1; i <= endBin1; ++i)
    {
        try
        {
            float mix = hardSwap ? 1.0f : band1Mix;
            mixedMagnitude1.at(i) = (1.0f - mix) * mainMagnitude.at(i) + mix * sidechainMagnitude.at(i);
            mixedPhase1.at(i) = (1.0f - mix) * mainPhase.at(i) + mix * sidechainPhase.at(i);
        }
        catch (const std::out_of_range& e)
        {
            DBG("Out of range access in band1 loop at index " << i << ": " << e.what());
        }
    }

    // 在 band2 频段内，独立地对主链和侧链进行交换/混合（不再依赖 band1 的位置或索引）
    for (int i = startBin2; i <= endBin2; ++i)
    {
        try
        {
            float mix = hardSwap ? 1.0f : band2Mix;
            mixedMagnitude2.at(i) = (1.0f - mix) * mainMagnitude.at(i) + mix * sidechainMagnitude.at(i);
            mixedPhase2.at(i) = (1.0f - mix) * mainPhase.at(i) + mix * sidechainPhase.at(i);
        }
        catch (const std::out_of_range& e)
        {
            DBG("Out of range access in band2 loop at index " << i << ": " << e.what());
        }
    }

    // 汇总成最终输出频谱：band1、band2 各自生效于自己的频率范围，其余频率保持主输入不变
    outMagnitude = mainMagnitude;
    outPhase = mainPhase;
    for (int i = startBin1; i <= endBin1; ++i)
    {
        outMagnitude[i] = mixedMagnitude1[i];
        outPhase[i] = mixedPhase1[i];
    }
    for (int i = startBin2; i <= endBin2; ++i)
    {
        outMagnitude[i] = mixedMagnitude2[i];
        outPhase[i] = mixedPhase2[i];
    }
}


void FreqWeaveAudioProcessor::performIFFT(float* data)
{
    // 注意：本函数只在 processFrame() 里调用，vectorMutex 已经在 processBlock() 里加过锁，
    // 这里不能再次加锁——std::mutex 不可重入，重复加锁会导致音频线程死锁。
    std::fill(data, data + 2 * fftSize, 0.0f);

    // 只需要填 [0 ~ fftSize/2] 这 fftSize/2 + 1 个复数 bin，JUCE 的实数逆变换只会用到这一半
    for (int i = 0; i <= fftSize / 2; ++i)
    {
        data[2 * i]     = outMagnitude[(size_t) i] * std::cos(outPhase[(size_t) i]);   // 实部
        data[2 * i + 1] = outMagnitude[(size_t) i] * std::sin(outPhase[(size_t) i]);   // 虚部
    }

    // DC(0Hz) 和 Nyquist 的虚部必须为 0
    data[1] = 0.0f;
    data[2 * (fftSize / 2) + 1] = 0.0f;

    // JUCE 的实数逆变换内部会做归一化，结果在前 fftSize 个元素里，不需要再手动除以 fftSize
    fft.performRealOnlyInverseTransform(data);
}




//==============================================================================
bool FreqWeaveAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* FreqWeaveAudioProcessor::createEditor()
{
    return new FreqWeaveAudioProcessorEditor (*this);
}

//==============================================================================
void FreqWeaveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // 使用 AudioProcessorValueTreeState 的 copyState 方法复制当前状态
    juce::ValueTree state = parameters.copyState();
    
    // 将 ValueTree 转换为 XML
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    
    // 如果 XML 有效，写入到内存块
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void FreqWeaveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // 从二进制数据中解析出 XML 元素
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    
    if (xmlState != nullptr)
    {
        // 确保 XML 标签名与当前状态的类型匹配
        if (xmlState->hasTagName (parameters.state.getType()))
        {
            // 从 XML 中恢复状态
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FreqWeaveAudioProcessor();
}
