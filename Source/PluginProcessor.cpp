/*
  RS Roar Ripper - Feedback Suppressor
  Copyright (c) 2026 Roar Sound

  SPDX-License-Identifier: AGPL-3.0-only

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published by
  the Free Software Foundation, version 3 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <https://www.gnu.org/licenses/>.

  This software uses the JUCE framework (https://juce.com), which is also
  licensed under the GNU Affero General Public License version 3.
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ids
{
    const juce::String sens = "sens";
    const juce::String hold = "hold";
    const juce::String release = "release";
    const juce::String hpfFreq = "hpfFreq";
    const juce::String hpfEn = "hpfEnabled";
    const juce::String lpfFreq = "lpfFreq";
    const juce::String lpfEn = "lpfEnabled";
    const juce::String speed = "speed";
    const juce::String mode = "mode";
    const juce::String ab = "ab";
    const juce::String mute = "mute";
}

// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout RoarRipperAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto unit = [](const char* u, int decimals)
        {
            return juce::AudioParameterFloatAttributes().withLabel(u).withStringFromValueFunction(
                [u, decimals](float v, int) { return juce::String(v, decimals) + " " + u; });
        };

    // Sensitivity: 70 %
    l.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ids::sens, 1 }, "Sensitivity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f,
        juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(
            [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; })));

    // Hold: 150 ms
    l.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ids::hold, 1 }, "Hold",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 1.0f), 150.0f, unit("ms", 0)));

    // Release: 1 s
    l.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ids::release, 1 }, "Release",
        juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 1.0f, unit("s", 2)));

    // HPF: 130 Hz
    l.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ids::hpfFreq, 1 }, "HPF Frequency",
        juce::NormalisableRange<float>(10.0f, 10000.0f, 1.0f, 0.3f), 130.0f, unit("Hz", 0)));

    // HPF enabled: true (live), standalone wird im Processor erzwungen aus
    l.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ids::hpfEn, 1 }, "HPF Enabled", true));

    // LPF: 20 kHz
    l.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ ids::lpfFreq, 1 }, "LPF Frequency",
        juce::NormalisableRange<float>(100.0f, 20000.0f, 1.0f, 0.3f), 20000.0f, unit("Hz", 0)));

    // LPF enabled: false
    l.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ids::lpfEn, 1 }, "LPF Enabled", false));

    // Speed: Cheetah (Index 3)
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ids::speed, 1 }, "Speed",
        juce::StringArray{ "Turtle", "Elephant", "Lion", "Cheetah" }, 3));

    // Mode
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ ids::mode, 1 }, "Mode",
        juce::StringArray{ "Live", "Statisch", "Standalone" }, 1));

    l.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ids::ab, 1 }, "Bypass", false));

    l.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ ids::mute, 1 }, "Mute", false));

    return l;
}

// ============================================================================
RoarRipperAudioProcessor::RoarRipperAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, juce::Identifier("RoarRipperState"), createLayout())
{
    pSens = parameters.getRawParameterValue(ids::sens);
    pHold = parameters.getRawParameterValue(ids::hold);
    pRelease = parameters.getRawParameterValue(ids::release);
    pHpfFreq = parameters.getRawParameterValue(ids::hpfFreq);
    pLpfFreq = parameters.getRawParameterValue(ids::lpfFreq);
    pHpfEn = parameters.getRawParameterValue(ids::hpfEn);
    pLpfEn = parameters.getRawParameterValue(ids::lpfEn);
    pSpeed = parameters.getRawParameterValue(ids::speed);
    pMode = parameters.getRawParameterValue(ids::mode);
    pAb = parameters.getRawParameterValue(ids::ab);
    pMute = parameters.getRawParameterValue(ids::mute);
}

RoarRipperAudioProcessor::~RoarRipperAudioProcessor() = default;

bool RoarRipperAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

// ============================================================================
void RoarRipperAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentRate.store(sampleRate);
    engine.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    lastFilterRate = -1.0;
    appliedHpf = -1.0f;
    appliedLpf = -1.0f;

    hpfGain = 0.0f;
    lpfGain = 0.0f;
    muteGain = 1.0f;
    wasActive = false;

    for (auto& f : hpf1) f.reset();
    for (auto& f : hpf2) f.reset();
    for (auto& f : hpf3) f.reset();
    for (auto& f : lpf1) f.reset();
    for (auto& f : lpf2) f.reset();

    cpuLoad.store(0.0f);
    inputLevel.store(-60.0f);
    outputLevel.store(-60.0f);

    updateSettings();
    applyPendingRestore();

    setLatencySamples(0);
}

// ============================================================================
void RoarRipperAudioProcessor::updateSettings() noexcept
{
    rsdsp::FeedbackEngine::Settings s;

    const int mode = juce::roundToInt(pMode->load());

    s.mode = (mode == 1) ? rsdsp::FeedbackEngine::Mode::setup
        : (mode == 2) ? rsdsp::FeedbackEngine::Mode::standalone
        : rsdsp::FeedbackEngine::Mode::live;

    s.sensitivity = juce::jlimit(0.0, 1.0, (double)pSens->load());
    s.holdSeconds = juce::jlimit(0.0, 2.0, (double)pHold->load() / 1000.0);
    s.releaseSeconds = juce::jlimit(0.001, 5.0, (double)pRelease->load());

    const int spd = juce::jlimit(0, 3, juce::roundToInt(pSpeed->load()));
    s.hopDivisor = (spd == 0) ? 8 : (spd == 1) ? 16 : (spd == 2) ? 32 : 64;

    const auto& cur = engine.getSettings();

    auto differs = [](double a, double b) { return std::abs(a - b) > 1.0e-9; };

    if (differs(s.sensitivity, cur.sensitivity)
        || differs(s.holdSeconds, cur.holdSeconds)
        || differs(s.releaseSeconds, cur.releaseSeconds)
        || s.hopDivisor != cur.hopDivisor
        || s.mode != cur.mode)
    {
        engine.setSettings(s);
    }
}

void RoarRipperAudioProcessor::applyPendingRestore() noexcept
{
    const int n = pendingCount.exchange(-1);
    if (n < 0)
        return;
    engine.replaceFixed(pending.data(), n);   // Import/State ersetzt die bisherigen festen Notches
}

// ============================================================================
float RoarRipperAudioProcessor::getHpfFreq() const noexcept
{
    return juce::jlimit(10.0f, 10000.0f, pHpfFreq->load());
}

float RoarRipperAudioProcessor::getLpfFreq() const noexcept
{
    return juce::jlimit(100.0f, 20000.0f, pLpfFreq->load());
}

bool RoarRipperAudioProcessor::isHpfEnabled() const noexcept
{
    if (getMode() == 2)
        return false;
    return pHpfEn->load() > 0.5f;
}

bool RoarRipperAudioProcessor::isLpfEnabled() const noexcept
{
    if (getMode() == 2)
        return false;
    return pLpfEn->load() > 0.5f;
}

// ============================================================================
void RoarRipperAudioProcessor::measureLevel(const juce::AudioBuffer<float>& buffer,
    int numChannels, int numSamples,
    std::atomic<float>& target) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
    {
        target.store(-60.0f);
        return;
    }

    float sum = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* d = buffer.getReadPointer(ch);
        for (int s = 0; s < numSamples; ++s)
            sum += d[s] * d[s];
    }

    const float rms = std::sqrt(sum / (float)(numChannels * numSamples));
    float db = 20.0f * std::log10(rms + 1.0e-10f);
    db = juce::jlimit(-60.0f, 5.0f, db);

    const float cur = target.load();
    const float f = (db > cur) ? 0.3f : 0.05f;
    target.store(cur + f * (db - cur));
}

void RoarRipperAudioProcessor::finishCpu(std::chrono::high_resolution_clock::time_point t0,
    int numSamples, double sr) noexcept
{
    const auto t1 = std::chrono::high_resolution_clock::now();
    const float procMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    const float availMs = (float)numSamples / (float)std::max(1.0, sr) * 1000.0f;

    if (availMs > 0.0f)
    {
        const float load = juce::jlimit(0.0f, 4.0f, procMs / availMs);
        cpuLoad.store(cpuLoad.load() + 0.1f * (load - cpuLoad.load()));
    }
}

// ============================================================================
void RoarRipperAudioProcessor::refreshToneCoeffs(double sr)
{
    const auto hc = juce::IIRCoefficients::makeHighPass(sr, (double)curHpf);
    const auto lc = juce::IIRCoefficients::makeLowPass(sr, (double)curLpf);

    for (auto& f : hpf1) f.setCoefficients(hc);
    for (auto& f : hpf2) f.setCoefficients(hc);
    for (auto& f : hpf3) f.setCoefficients(hc);

    for (auto& f : lpf1) f.setCoefficients(lc);
    for (auto& f : lpf2) f.setCoefficients(lc);

    appliedHpf = curHpf;
    appliedLpf = curLpf;
}

void RoarRipperAudioProcessor::processToneFilters(juce::AudioBuffer<float>& buffer,
    int numChannels, int numSamples, double sr)
{
    const bool standalone = (getMode() == 2);

    const bool hpfOn = standalone ? false : pHpfEn->load() > 0.5f;
    const bool lpfOn = standalone ? false : pLpfEn->load() > 0.5f;

    const float targetHpf = juce::jlimit(10.0f, 10000.0f, pHpfFreq->load());
    const float targetLpf = juce::jlimit(100.0f, 20000.0f, pLpfFreq->load());

    if (sr != lastFilterRate)
    {
        lastFilterRate = sr;
        curHpf = targetHpf;
        curLpf = targetLpf;
        refreshToneCoeffs(sr);

        for (auto& f : hpf1) f.reset();
        for (auto& f : hpf2) f.reset();
        for (auto& f : hpf3) f.reset();
        for (auto& f : lpf1) f.reset();
        for (auto& f : lpf2) f.reset();
    }

    const int ch = std::min(numChannels, (int)rsdsp::kMaxChannels);

    const float slew = 1.0f - std::exp(-1.0f / std::max(1.0f, 0.02f * (float)sr));
    const float gainCoeff = 1.0f / std::max(1.0f, 0.01f * (float)sr);

    for (int s = 0; s < numSamples; ++s)
    {
        curHpf += (targetHpf - curHpf) * slew;
        curLpf += (targetLpf - curLpf) * slew;

        hpfGain += ((hpfOn ? 1.0f : 0.0f) - hpfGain) * gainCoeff;
        lpfGain += ((lpfOn ? 1.0f : 0.0f) - lpfGain) * gainCoeff;

        if ((s & 31) == 0
            && (std::abs(curHpf - appliedHpf) > 0.05f || std::abs(curLpf - appliedLpf) > 0.05f))
        {
            refreshToneCoeffs(sr);
        }

        for (int c = 0; c < ch; ++c)
        {
            float* d = buffer.getWritePointer(c);
            float x = d[s];

            float h = hpf1[(size_t)c].processSingleSampleRaw(x);
            h = hpf2[(size_t)c].processSingleSampleRaw(h);
            h = hpf3[(size_t)c].processSingleSampleRaw(h);
            x = x + hpfGain * (h - x);

            float l = lpf1[(size_t)c].processSingleSampleRaw(x);
            l = lpf2[(size_t)c].processSingleSampleRaw(l);
            x = x + lpfGain * (l - x);

            d[s] = x;
        }
    }
}

// ============================================================================
void RoarRipperAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const auto t0 = std::chrono::high_resolution_clock::now();

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    double sr = getSampleRate();
    if (sr <= 0.0)
        sr = 48000.0;

    measureLevel(buffer, numChannels, numSamples, inputLevel);

    if (resetRequested.exchange(false))
        engine.reset();

    if (fixRequested.exchange(false))
        engine.fixAllCurrent();

    if (measureStartRequested.exchange(false))
        engine.resetDetector();

    applyPendingRestore();
    updateSettings();

    // Messung in Live und Statisch, nicht im Standalone
    if (getMode() == 2)
        measuring.store(false);
    engine.setMeasuring(measuring.load());

    const bool muted = pMute->load() > 0.5f;
    const bool bypassed = pAb->load() > 0.5f;

    if (muted || muteGain < 0.999f)
    {
        const float target = muted ? 0.0f : 1.0f;
        const float coeff = 1.0f / std::max(1.0f, 0.005f * (float)sr);

        for (int s = 0; s < numSamples; ++s)
        {
            muteGain += (target - muteGain) * coeff;
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer(ch)[s] *= muteGain;
        }
    }

    const bool active = !muted && !bypassed;

    if (active && !wasActive)
        engine.resetDetector();

    wasActive = active;

    if (muted)
    {
        measureLevel(buffer, numChannels, numSamples, outputLevel);
        finishCpu(t0, numSamples, sr);
        return;
    }

    if (bypassed)
    {
        outputLevel.store(inputLevel.load());
        finishCpu(t0, numSamples, sr);
        return;
    }

    processToneFilters(buffer, numChannels, numSamples, sr);

    engine.process(buffer.getArrayOfWritePointers(), numChannels, numSamples);

    measureLevel(buffer, numChannels, numSamples, outputLevel);
    finishCpu(t0, numSamples, sr);
}

// ============================================================================
void RoarRipperAudioProcessor::buildFixedTree(juce::ValueTree& state)
{
    state.removeChild(state.getChildWithName("FixedNotches"), nullptr);

    juce::ValueTree fixed("FixedNotches");
    int n = 0;

    for (const auto& p : engine.getBank().published())
    {
        if (n >= rsdsp::kFixedSlots)
            break;

        if (p.active.load() && p.fixed.load() && p.depthDb.load() < -0.5f)
        {
            juce::ValueTree nt("Notch");
            nt.setProperty("freq", p.freq.load(), nullptr);
            nt.setProperty("depth", p.depthDb.load(), nullptr);
            fixed.appendChild(nt, nullptr);
            ++n;
        }
    }

    state.appendChild(fixed, nullptr);
}

void RoarRipperAudioProcessor::extractFixed(const juce::ValueTree& state) noexcept
{
    int n = 0;
    for (auto child : state.getChildWithName("FixedNotches"))
    {
        if (n >= rsdsp::kFixedSlots)
            break;

        const float f = child.getProperty("freq", 0.0f);
        const float d = child.getProperty("depth", 0.0f);

        if (f > 20.0f && d < -0.5f)
            pending[(size_t)n++] = { f, juce::jlimit((float)rsdsp::kMaxCutDb, 0.0f, d) };
    }
    pendingCount.store(n);
}

void RoarRipperAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    buildFixedTree(state);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void RoarRipperAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.hasType(parameters.state.getType()))
        return;

    extractFixed(state);
    state.removeChild(state.getChildWithName("FixedNotches"), nullptr);
    parameters.replaceState(state);
}

bool RoarRipperAudioProcessor::exportToFile(const juce::File& file)
{
    auto state = parameters.copyState();
    buildFixedTree(state);
    if (auto xml = state.createXml())
        return file.replaceWithText(xml->toString());
    return false;
}

bool RoarRipperAudioProcessor::importFromFile(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (xml == nullptr)
        return false;

    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.hasType(parameters.state.getType()))
        return false;

    extractFixed(state);
    state.removeChild(state.getChildWithName("FixedNotches"), nullptr);
    parameters.replaceState(state);
    return true;
}

// ============================================================================
juce::AudioProcessorEditor* RoarRipperAudioProcessor::createEditor()
{
    return new RoarRipperAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RoarRipperAudioProcessor();
}