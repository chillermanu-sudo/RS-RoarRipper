#pragma once

#include <JuceHeader.h>
#include "dsp/FeedbackEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <vector>

class RoarRipperAudioProcessorEditor;

class RoarRipperAudioProcessor : public juce::AudioProcessor
{
public:
    RoarRipperAudioProcessor();
    ~RoarRipperAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool exportToFile(const juce::File& file);
    bool importFromFile(const juce::File& file);

    void requestReset()  noexcept { resetRequested.store(true); }
    void requestFixAll() noexcept { fixRequested.store(true); }

    void startMeasurement() noexcept { measureStartRequested.store(true); measuring.store(true); }
    void stopMeasurement()  noexcept { measuring.store(false); fixRequested.store(true); }
    bool isMeasuring() const noexcept { return measuring.load(); }

    const rsdsp::FeedbackEngine& getEngine() const noexcept { return engine; }

    double getAnalysisSampleRate() const noexcept { return currentRate.load(); }
    int    getMode() const noexcept { return juce::roundToInt(pMode->load()); }

    float getCpuLoad()     const noexcept { return cpuLoad.load(); }
    float getInputLevel()  const noexcept { return inputLevel.load(); }
    float getOutputLevel() const noexcept { return outputLevel.load(); }

    float getHpfFreq() const noexcept;
    float getLpfFreq() const noexcept;
    bool  isHpfEnabled() const noexcept;
    bool  isLpfEnabled() const noexcept;

    juce::AudioProcessorValueTreeState parameters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void updateSettings() noexcept;
    void applyPendingRestore() noexcept;
    void processToneFilters(juce::AudioBuffer<float>&, int numChannels, int numSamples, double sr);
    void refreshToneCoeffs(double sr);

    void measureLevel(const juce::AudioBuffer<float>&, int numChannels, int numSamples,
        std::atomic<float>& target) noexcept;
    void finishCpu(std::chrono::high_resolution_clock::time_point t0,
        int numSamples, double sr) noexcept;

    void buildFixedTree(juce::ValueTree& state);
    void extractFixed(const juce::ValueTree& state) noexcept;

    rsdsp::FeedbackEngine engine;

    std::atomic<double> currentRate{ 48000.0 };
    std::atomic<bool> resetRequested{ false };
    std::atomic<bool> fixRequested{ false };
    std::atomic<bool> measuring{ false };
    std::atomic<bool> measureStartRequested{ false };

    std::array<rsdsp::FeedbackEngine::StoredNotch, rsdsp::kFixedSlots> pending{};
    std::atomic<int> pendingCount{ -1 };

    std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pHpfFreq = nullptr;
    std::atomic<float>* pLpfFreq = nullptr;
    std::atomic<float>* pHpfEn = nullptr;
    std::atomic<float>* pLpfEn = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pAb = nullptr;
    std::atomic<float>* pMute = nullptr;

    std::array<juce::IIRFilter, rsdsp::kMaxChannels> hpf1, hpf2, hpf3, lpf1, lpf2;

    double lastFilterRate = -1.0;
    float  curHpf = 130.0f, curLpf = 20000.0f;
    float  appliedHpf = -1.0f, appliedLpf = -1.0f;
    float  hpfGain = 0.0f, lpfGain = 0.0f, muteGain = 1.0f;

    bool wasActive = false;

    std::atomic<float> cpuLoad{ 0.0f };
    std::atomic<float> inputLevel{ -60.0f };
    std::atomic<float> outputLevel{ -60.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoarRipperAudioProcessor)
};