/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// 深色主题配色，粉色只作为强调色用在少数几个地方（标题、旋钮指示、开关、band1 频段高亮）
namespace
{
    const juce::Colour kAccentPink   (0xffff4fa0);
    const juce::Colour kAccentBlue   (0xff63d2ff);
    const juce::Colour kAccentOrange (0xffffb454);
    const juce::Colour kText         (0xffe8e8ec);
    const juce::Colour kPanel        (0xff26262c);
    const juce::Colour kPanelOutline (0xff3c3c44);
    const juce::Colour kBackground   (0xff1c1c20);
}

//==============================================================================
FreqWeaveAudioProcessorEditor::FreqWeaveAudioProcessorEditor (FreqWeaveAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // ---- 深色主题 + 粉色点缀 ----
    lookAndFeel.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, kAccentPink);
    lookAndFeel.setColour (juce::Slider::thumbColourId, kAccentPink);
    lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, kPanelOutline);
    lookAndFeel.setColour (juce::Slider::textBoxTextColourId, kText);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, kPanelOutline);
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, kAccentPink);
    lookAndFeel.setColour (juce::ToggleButton::textColourId, kText);
    lookAndFeel.setColour (juce::GroupComponent::outlineColourId, kPanelOutline);
    lookAndFeel.setColour (juce::GroupComponent::textColourId, kText);
    lookAndFeel.setColour (juce::Label::textColourId, kText);
    setLookAndFeel (&lookAndFeel);

    setSize (560, 420);

    // ---- 标题 / 提示 ----
    titleLabel.setText ("FreqWeave", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f).withStyleFlags (juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, kAccentPink);
    addAndMakeVisible (titleLabel);

    sidechainInstructionLabel.setText ("Please set your SideChain in your DAW.", juce::dontSendNotification);
    sidechainInstructionLabel.setFont (juce::Font (juce::FontOptions (13.0f).withStyleFlags (juce::Font::italic)));
    sidechainInstructionLabel.setColour (juce::Label::textColourId, kText.withAlpha (0.75f));
    addAndMakeVisible (sidechainInstructionLabel);

    // ---- 分组框：Band1 / Band2 各自独立一组，公共参数放 Global ----
    band1GroupBox.setText ("Band 1");
    band2GroupBox.setText ("Band 2");
    globalGroupBox.setText ("Global");
    addAndMakeVisible (band1GroupBox);
    addAndMakeVisible (band2GroupBox);
    addAndMakeVisible (globalGroupBox);

    // ---- 滑块（统一用旋钮样式） ----
    setupSlider (cutFrequencyFrom1Slider, "CutFrequencyFrom1");
    setupSlider (cutFrequencyFrom2Slider, "CutFrequencyFrom2");
    setupSlider (frequencyBandLengthSlider, "FrequencyBandLength");
    setupSlider (band1MixSlider, "Band1Mix");
    setupSlider (band2MixSlider, "Band2Mix");

    cutFrequencyFrom1Slider.addListener (this);
    cutFrequencyFrom2Slider.addListener (this);
    frequencyBandLengthSlider.addListener (this);
    band1MixSlider.addListener (this);
    band2MixSlider.addListener (this);

    // 注意：下面创建的 SliderAttachment 会把滑块的范围和文字显示函数都换成参数自带的，
    // 所以滑块的范围、数值显示格式都在创建 attachment 之后再设置

    // Exchange 开关：关 = 混合模式（用 Mix 旋钮按比例混合），开 = 硬切换模式（该 band 内完全换成侧链）
    exchangeBandOrNotButton.setButtonText ("Swap Mode");
    addAndMakeVisible (exchangeBandOrNotButton);

    // ---- 参数绑定 ----
    cutFrequencyFrom1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "cutFrequencyFrom1", cutFrequencyFrom1Slider);
    cutFrequencyFrom2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "cutFrequencyFrom2", cutFrequencyFrom2Slider);
    frequencyBandLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "FrequencyBandLength", frequencyBandLengthSlider);
    exchangeBandOrNotAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.parameters, "ExchangeBandValue", exchangeBandOrNotButton);
    band1MixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "band1Mix", band1MixSlider);
    band2MixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "band2Mix", band2MixSlider);

    // ---- 数值显示格式（必须在 attachment 之后设置，见上面的说明） ----
    auto hzText = [] (double value) { return juce::String (juce::roundToInt (value)) + " Hz"; };
    auto hzValue = [] (const juce::String& text) { return text.getDoubleValue(); };
    cutFrequencyFrom1Slider.textFromValueFunction = hzText;
    cutFrequencyFrom1Slider.valueFromTextFunction = hzValue;
    cutFrequencyFrom2Slider.textFromValueFunction = hzText;
    cutFrequencyFrom2Slider.valueFromTextFunction = hzValue;

    // Band Length 参数本身是 [0, 2]，对应 0.1 ~ 3 个倍频程（跟 crossSynthesis() 里的映射一致）
    frequencyBandLengthSlider.textFromValueFunction = [] (double value)
    {
        return juce::String (juce::jmap (value, 0.0, 2.0, 0.1, 3.0), 2) + " oct";
    };
    frequencyBandLengthSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        return juce::jlimit (0.0, 2.0, juce::jmap (text.getDoubleValue(), 0.1, 3.0, 0.0, 2.0));
    };

    // Mix 参数本身是 [0, 1]，界面上用百分比显示
    auto percentText = [] (double value) { return juce::String (juce::roundToInt (value * 100.0)) + " %"; };
    auto percentValue = [] (const juce::String& text) { return juce::jlimit (0.0, 1.0, text.getDoubleValue() / 100.0); };
    band1MixSlider.textFromValueFunction = percentText;
    band1MixSlider.valueFromTextFunction = percentValue;
    band2MixSlider.textFromValueFunction = percentText;
    band2MixSlider.valueFromTextFunction = percentValue;

    for (auto* s : { &cutFrequencyFrom1Slider, &cutFrequencyFrom2Slider, &frequencyBandLengthSlider,
                     &band1MixSlider, &band2MixSlider })
        s->updateText();

    // Swap Mode 打开时 Mix 旋钮不起作用（频段直接整段换成侧链），把它们置灰，免得拧了没反应。
    // 开关的状态变化（用户点击、宿主自动化）都会触发 onStateChange。
    exchangeBandOrNotButton.onStateChange = [this]
    {
        const bool mixUsable = ! exchangeBandOrNotButton.getToggleState();
        for (auto* c : std::initializer_list<juce::Component*> { &band1MixSlider, &band2MixSlider,
                                                                 &band1MixLabel, &band2MixLabel })
            c->setEnabled (mixUsable);
    };
    exchangeBandOrNotButton.onStateChange();

    // ---- 每个旋钮上方的小标签 ----
    auto setupSmallLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, kText);
        addAndMakeVisible (label);
    };

    setupSmallLabel (cutFrequencyFrom1Label, "Cut From 1");
    setupSmallLabel (cutFrequencyFrom2Label, "Cut From 2");
    setupSmallLabel (frequencyBandLengthLabel, "Band Length");
    setupSmallLabel (band1MixLabel, "Band 1 Mix");
    setupSmallLabel (band2MixLabel, "Band 2 Mix");

    startTimerHz (30); // 频谱显示定时刷新
}

FreqWeaveAudioProcessorEditor::~FreqWeaveAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void FreqWeaveAudioProcessorEditor::sliderValueChanged (juce::Slider* slider)
{
    DBG (slider->getName() << " changed to " << slider->getValue());
}

void FreqWeaveAudioProcessorEditor::timerCallback()
{
    if (! spectrumBounds.isEmpty())
        repaint (spectrumBounds);
}

void FreqWeaveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    // 顶部一条细细的粉色线，算是外观上的一点点缀，不铺满整个界面
    g.setColour (kAccentPink.withAlpha (0.6f));
    g.fillRect (getLocalBounds().removeFromTop (3));

    drawSpectrum (g, spectrumBounds);
}

void FreqWeaveAudioProcessorEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (bounds.getWidth() < 4 || bounds.getHeight() < 4)
        return;

    g.setColour (kPanel);
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    const int fftSize = FreqWeaveAudioProcessor::fftSize;
    const int numBins = fftSize / 2;
    const double sr = audioProcessor.sampleRate > 0.0 ? audioProcessor.sampleRate : 44100.0;

    // 从音频线程安全地拷贝一份频谱数据出来画，避免和 processBlock() 竞争同一个 vector
    std::vector<float> mainMag, sideMag, outMag;
    {
        const juce::ScopedLock sl (audioProcessor.bufferLock);
        mainMag = audioProcessor.mainMagnitude;
        sideMag = audioProcessor.sidechainMagnitude;
        outMag  = audioProcessor.outMagnitude;
    }

    if ((int) mainMag.size() < numBins)
    {
        g.setColour (kPanelOutline);
        g.drawRoundedRectangle (bounds.toFloat(), 6.0f, 1.0f);
        return;
    }

    auto freqToX = [&] (float freq) -> float
    {
        freq = juce::jlimit (20.0f, 20000.0f, freq);
        float logPos = std::log10 (freq / 20.0f) / std::log10 (20000.0f / 20.0f);
        return (float) bounds.getX() + logPos * (float) bounds.getWidth();
    };

    auto magToY = [&] (float mag) -> float
    {
        // 加了 Hann 窗之后，振幅为 A 的正弦在 FFT 里的峰值约为 A * fftSize / 4，
        // 这里除掉它，纵轴才大致对应 dBFS（0 dB 对应满幅正弦），否则正常音量的信号会顶到图的最上面
        float db = 20.0f * std::log10 (mag * (4.0f / (float) fftSize) + 1.0e-6f);
        float norm = juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
        return (float) bounds.getBottom() - norm * (float) bounds.getHeight();
    };

    auto buildPath = [&] (const std::vector<float>& mag) -> juce::Path
    {
        juce::Path path;
        bool started = false;
        for (int bin = 1; bin < numBins; ++bin)
        {
            float freq = (float) (bin * sr / fftSize);
            if (freq < 20.0f || freq > 20000.0f)
                continue;

            float x = freqToX (freq);
            float y = magToY (mag[(size_t) bin]);

            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                path.lineTo (x, y);
            }
        }
        return path;
    };

    // 叠加 band1、band2 当前覆盖的频率范围，方便直接在频谱上看到设的是哪一段
    auto* cutParam1   = audioProcessor.parameters.getRawParameterValue ("cutFrequencyFrom1");
    auto* cutParam2   = audioProcessor.parameters.getRawParameterValue ("cutFrequencyFrom2");
    auto* lengthParam = audioProcessor.parameters.getRawParameterValue ("FrequencyBandLength");

    if (cutParam1 != nullptr && cutParam2 != nullptr && lengthParam != nullptr)
    {
        // 跟 PluginProcessor::crossSynthesis() 里用的是同一套倍频程公式，
        // 不然这里画的高亮范围会跟实际处理的频段对不上
        float octaves = juce::jmap (lengthParam->load(), 0.0f, 2.0f, 0.1f, 3.0f);
        float octaveFactor = std::pow (2.0f, octaves * 0.5f);

        auto drawBandOverlay = [&] (float centreFreq, juce::Colour colour)
        {
            float x1 = freqToX (centreFreq / octaveFactor);
            float x2 = freqToX (centreFreq * octaveFactor);
            g.setColour (colour.withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (x1, (float) bounds.getY(), juce::jmax (2.0f, x2 - x1), (float) bounds.getHeight()));
        };

        drawBandOverlay (cutParam1->load(), kAccentPink);
        drawBandOverlay (cutParam2->load(), kAccentBlue);
    }

    // 主输入频谱
    g.setColour (kText.withAlpha (0.85f));
    g.strokePath (buildPath (mainMag), juce::PathStrokeType (1.5f));

    // 侧链频谱（只有激活时才画，颜色区分开）
    const bool sidechainActive = audioProcessor.isSidechainInputActive();
    if (sidechainActive)
    {
        g.setColour (kAccentOrange.withAlpha (0.75f));
        g.strokePath (buildPath (sideMag), juce::PathStrokeType (1.2f));
    }

    // 插件实际输出的频谱（粉色）：band 范围内应该跟随侧链（橙线），band 之外跟随主输入（白线）
    if ((int) outMag.size() >= numBins)
    {
        g.setColour (kAccentPink.withAlpha (0.95f));
        g.strokePath (buildPath (outMag), juce::PathStrokeType (1.6f));
    }

    // 图例：一眼看出哪条线是哪个信号，以及插件有没有认为侧链已连接
    {
        auto legend = bounds.reduced (8, 4).removeFromTop (14);
        g.setFont (11.0f);
        auto drawLegendItem = [&] (juce::Colour colour, const juce::String& text)
        {
            const int textWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 4;
            g.setColour (colour);
            g.fillRect (legend.getX(), legend.getCentreY() - 1, 12, 2);
            g.setColour (kText.withAlpha (0.8f));
            g.drawText (text, legend.getX() + 16, legend.getY(), textWidth, legend.getHeight(), juce::Justification::centredLeft);
            legend.removeFromLeft (16 + textWidth + 10);
        };
        drawLegendItem (kText, "main in");
        drawLegendItem (kAccentOrange, sidechainActive ? "sidechain in" : "sidechain (not connected)");
        drawLegendItem (kAccentPink, "output");
    }

    g.setColour (kPanelOutline);
    g.drawRoundedRectangle (bounds.toFloat(), 6.0f, 1.0f);
}

void FreqWeaveAudioProcessorEditor::setupSlider (juce::Slider& slider, const juce::String& name)
{
    slider.setName (name);
    slider.setSliderStyle (juce::Slider::Rotary);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
    addAndMakeVisible (slider);
}

void FreqWeaveAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleLabel.setBounds (area.removeFromTop (28));

    sidechainInstructionLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (8);

    spectrumBounds = area.removeFromTop (110);
    area.removeFromTop (12);

    const int groupSpacing = 12;
    const int groupWidth = (area.getWidth() - groupSpacing * 2) / 3;

    auto band1Area  = area.removeFromLeft (groupWidth);
    area.removeFromLeft (groupSpacing);
    auto band2Area  = area.removeFromLeft (groupWidth);
    area.removeFromLeft (groupSpacing);
    auto globalArea = area;

    band1GroupBox.setBounds (band1Area);
    band2GroupBox.setBounds (band2Area);
    globalGroupBox.setBounds (globalArea);

    auto layoutKnobPair = [] (juce::Rectangle<int> box,
                              juce::Component& labelA, juce::Component& compA,
                              juce::Component& labelB, juce::Component& compB)
    {
        box.removeFromTop (24); // 给 GroupComponent 的标题栏留空间
        box.reduce (8, 8);

        auto left  = box.removeFromLeft (box.getWidth() / 2);
        auto right = box;

        labelA.setBounds (left.removeFromTop (18));
        compA.setBounds (left);

        labelB.setBounds (right.removeFromTop (18));
        compB.setBounds (right);
    };

    layoutKnobPair (band1Area, cutFrequencyFrom1Label, cutFrequencyFrom1Slider, band1MixLabel, band1MixSlider);
    layoutKnobPair (band2Area, cutFrequencyFrom2Label, cutFrequencyFrom2Slider, band2MixLabel, band2MixSlider);

    auto globalContent = globalArea;
    globalContent.removeFromTop (24);
    globalContent.reduce (8, 8);

    auto bandLengthArea = globalContent.removeFromTop (globalContent.getHeight() * 6 / 10);
    frequencyBandLengthLabel.setBounds (bandLengthArea.removeFromTop (18));
    frequencyBandLengthSlider.setBounds (bandLengthArea);

    globalContent.removeFromTop (8);
    exchangeBandOrNotButton.setBounds (globalContent);
}
