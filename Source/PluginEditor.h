#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <memory>
#include <vector>

namespace rsui
{
    const juce::Colour panelHi{ 0xffc6c1a6 };
    const juce::Colour panelMid{ 0xffa49f85 };
    const juce::Colour panelLo{ 0xff7c7862 };
    const juce::Colour panelEdge{ 0xff54513f };
    const juce::Colour ink{ 0xff2e2c23 };
    const juce::Colour cream{ 0xfff4ecd8 };
    const juce::Colour vuPaper{ 0xfff1e7cd };
    const juce::Colour vuPaper2{ 0xffddcfab };
    const juce::Colour redZone{ 0xffb23a2b };
    const juce::Colour jewelGrn{ 0xff8fbf5a };
    const juce::Colour jewelAmb{ 0xffd79a3c };
    const juce::Colour jewelRed{ 0xffd24a34 };
    const juce::Colour glass{ 0xff0d1109 };
    const juce::Colour phosphor{ 0xff9ad46a };
    const juce::Colour chrome{ 0xffdedbd2 };
    const juce::Colour chromeDk{ 0xff6d6c62 };
    const juce::Colour bakelite{ 0xff1a1a1a };

    const juce::Colour plasticHi{ 0xffece3cd };
    const juce::Colour plasticMid{ 0xffd8cdb2 };
    const juce::Colour plasticLo{ 0xffb2a88e };
    const juce::Colour plasticShadow{ 0xff6d6857 };

    inline juce::Colour mix(juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith(b, t); }

    void engravedText(juce::Graphics&, const juce::String&, juce::Rectangle<float>,
        juce::Font, juce::Justification);
    void drawScrew(juce::Graphics&, juce::Point<float>, float, juce::Random&);
    void drawWear(juce::Graphics&, juce::Rectangle<float>, juce::Random&, int scratches, float strength);
    void drawRoarKnob(juce::Graphics&, juce::Point<float> centre, float radius,
        float angle, float scale, bool lit, juce::Random&);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
            bool over, bool down) override;
        juce::Font getTextButtonFont(juce::TextButton&, int h) override
        {
            return juce::FontOptions(juce::jmax(8.0f, (float)h * 0.42f), juce::Font::bold);
        }
    };
}

// ---------------------------------------------------------------------------
class InfoLabel : public juce::Label
{
public:
    void setInfoText(const juce::String& t) { setTooltip(t); }
};

// ---------------------------------------------------------------------------
class IconMetalButton : public juce::Button
{
public:
    explicit IconMetalButton(const juce::String& n) : juce::Button(n) {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
protected:
    virtual void drawIcon(juce::Graphics&, juce::Rectangle<float>) = 0;
    virtual juce::Colour litColour() const { return rsui::cream; }
};

class InfoButton : public IconMetalButton
{
public:
    InfoButton() : IconMetalButton("Info") {}
protected:
    void drawIcon(juce::Graphics&, juce::Rectangle<float>) override;
    juce::Colour litColour() const override { return rsui::jewelGrn; }
};

class CoffeeButton : public IconMetalButton
{
public:
    CoffeeButton() : IconMetalButton("Spenden") {}
protected:
    void drawIcon(juce::Graphics&, juce::Rectangle<float>) override;
};

class BugButton : public IconMetalButton
{
public:
    BugButton() : IconMetalButton("Bugreport") {}
protected:
    void drawIcon(juce::Graphics&, juce::Rectangle<float>) override;
};

class FlagButton : public IconMetalButton
{
public:
    FlagButton() : IconMetalButton("Sprache") {}
    void setEnglish(bool e) { english = e; repaint(); }
    bool isEnglish() const noexcept { return english; }
protected:
    void drawIcon(juce::Graphics&, juce::Rectangle<float>) override;
private:
    bool english = false;
};

// ---------------------------------------------------------------------------
class ToggleSwitch : public juce::Button,
    private juce::Timer
{
public:
    explicit ToggleSwitch(const juce::String& caption);
    void setCaption(const juce::String& c) { caption = c; repaint(); }
    void paintButton(juce::Graphics&, bool over, bool down) override;

protected:
    void buttonStateChanged() override;

private:
    void timerCallback() override;
    juce::String caption;
    float pos = 0.0f;
    float target = 0.0f;
};

// ---------------------------------------------------------------------------
class RetroKnob : public juce::Slider
{
public:
    RetroKnob();
    void setPowerParameter(juce::AudioProcessorValueTreeState&, const juce::String& paramID);
    bool hasPower() const noexcept { return powerParam != nullptr; }
    bool isPowerOn() const noexcept;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::AudioParameterBool* powerParam = nullptr;
    bool moved = false;
};

// ---------------------------------------------------------------------------
class ModeSelector : public juce::Slider
{
public:
    ModeSelector();
    void setLabels(const juce::String& a, const juce::String& b, const juce::String& c);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Point<float> knobCentre() const;
    float knobRadius() const;
    static float posAngle(int i);
    juce::String labels[3];
};

// ---------------------------------------------------------------------------
class SpeedSelector : public juce::Slider
{
public:
    SpeedSelector();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Point<float> knobCentre() const;
    float knobRadius() const;
    static float posAngle(int i);
};

// ---------------------------------------------------------------------------
class VuMeter : public juce::Component,
    public juce::TooltipClient
{
public:
    explicit VuMeter(const juce::String& caption);

    void setLevelDb(float dbFS) noexcept { targetVu = dbFS - referenceDbFS; }
    void update(double dtSeconds);
    void setTip(const juce::String& t) { tooltip = t; }

    void paint(juce::Graphics&) override;
    void resized() override;
    juce::String getTooltip() override { return tooltip; }

private:
    void buildFace();
    float needleAngle(float vu) const;

    juce::String caption, tooltip;
    juce::Image face;
    float currentVu = -24.0f;
    float targetVu = -24.0f;
    float peakHold = 0.0f;
    static constexpr float referenceDbFS = -18.0f;
};

// ---------------------------------------------------------------------------
class SpectrumView : public juce::Component,
    public juce::TooltipClient
{
public:
    explicit SpectrumView(RoarRipperAudioProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void setTip(const juce::String& t) { tooltip = t; }
    juce::String getTooltip() override { return tooltip; }

private:
    float xFor(double f, juce::Rectangle<float> a) const;
    float yFor(double db, juce::Rectangle<float> a) const;
    RoarRipperAudioProcessor& proc;
    juce::String tooltip;
    std::vector<juce::Point<float>> pts;
};

// ---------------------------------------------------------------------------
class InfoPopup : public juce::Component
{
public:
    InfoPopup();
    void present(const juce::String& text, juce::Point<int> mouseLocal,
        juce::Rectangle<int> bounds, float scale);
    void paint(juce::Graphics&) override;

private:
    juce::TextLayout layout;
    float padX = 12.0f;
    float padY = 9.0f;
    float lastScale = 1.0f;
};

// ---------------------------------------------------------------------------
class BugReportOverlay : public juce::Component
{
public:
    explicit BugReportOverlay(RoarRipperAudioProcessor& p);
    void show();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> getPanelBounds() const;
    juce::String gatherSnapshot() const;
    juce::String buildReportBody(const juce::String& comment) const;
    void doSend();

    RoarRipperAudioProcessor& proc;
    juce::Label titleLabel, hintLabel;
    juce::TextEditor infoBox, commentBox;
    juce::TextButton sendButton{ "Senden" };
    juce::TextButton cancelButton{ "Abbrechen" };
};

// ---------------------------------------------------------------------------
class RoarRipperAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit RoarRipperAudioProcessorEditor(RoarRipperAudioProcessor&);
    ~RoarRipperAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateVisibility();
    void setupInfoTexts();
    void setLanguage(bool en);
    void buildPanel();
    void updateInfoHover(double dt);

    struct Knob
    {
        RetroKnob slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob(Knob&, const juce::String& paramId, const juce::String& name);

    RoarRipperAudioProcessor& proc;
    rsui::LookAndFeel lnf;

    SpectrumView spectrum;
    BugReportOverlay bugOverlay;
    VuMeter vuIn, vuOut;
    InfoPopup infoPopup;

    InfoLabel versionLabel, cpuLabel;
    juce::Label speedLabel;

    ModeSelector  modeSelector;
    SpeedSelector speedSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modeAtt, speedAtt;

    ToggleSwitch measureSwitch, bypassSwitch, muteSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> abAtt, muteAtt;

    juce::TextButton resetButton{ "RESET" }, exportButton{ "EXPORT" }, importButton{ "IMPORT" };

    InfoButton   infoButton;
    FlagButton   flagButton;
    CoffeeButton coffeeButton;
    BugButton    bugButton;

    Knob sens, hold, release, hpf, lpf;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Image panel;
    juce::Rectangle<int> lampRect;
    float uiScale = 1.0f;
    double lastTick = 0.0;

    bool infoMode = false;
    bool english = false;

    std::vector<juce::Component*> infoTargets;
    juce::Component* hoverTarget = nullptr;
    juce::Point<int> lastMouse;
    double hoverSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoarRipperAudioProcessorEditor)
};