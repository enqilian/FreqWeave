/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class FreqWeaveAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                           public juce::Slider::Listener,
                                           private juce::Timer
{
public:
    FreqWeaveAudioProcessorEditor (FreqWeaveAudioProcessor&);
    ~FreqWeaveAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void sliderValueChanged(juce::Slider* slider) override;

private:
    void timerCallback() override; // 定时刷新频谱显示

    // 把主链/侧链的频谱画在给定区域里，并叠加 band1/band2 当前覆盖的频率范围
    void drawSpectrum(juce::Graphics& g, juce::Rectangle<int> bounds);

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    FreqWeaveAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreqWeaveAudioProcessorEditor)

    // 深色主题 + 粉色点缀
    juce::LookAndFeel_V4 lookAndFeel;

    juce::Label titleLabel;
    juce::Label sidechainInstructionLabel;

    // band1、band2 各自的分组框（视觉上明确区分两组控件）
    juce::GroupComponent band1GroupBox;
    juce::GroupComponent band2GroupBox;
    juce::GroupComponent globalGroupBox;

    juce::Slider cutFrequencyFrom1Slider; //第一个交换的band的frequency开始值
    juce::Slider cutFrequencyFrom2Slider; //第二个交换的band的frequency开始值
    juce::Slider frequencyBandLengthSlider; //band的frequency长度的值（两个band共用）
    juce::ToggleButton exchangeBandOrNotButton; //两个band是否交换（共用），做成开关而不是滑块
    juce::Slider band1MixSlider;
    juce::Slider band2MixSlider;

    //滑块/按钮附件
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutFrequencyFrom1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutFrequencyFrom2Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyBandLengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> exchangeBandOrNotAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> band1MixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> band2MixAttachment;

    juce::Label cutFrequencyFrom1Label;
    juce::Label cutFrequencyFrom2Label;
    juce::Label frequencyBandLengthLabel;
    juce::Label band1MixLabel;
    juce::Label band2MixLabel;

    // 频谱显示区域（在 resized() 里计算，paint()/timerCallback() 里使用）
    juce::Rectangle<int> spectrumBounds;

    // 帮助函数，用于设置滑块
    void setupSlider(juce::Slider& slider, const juce::String& name);
};
