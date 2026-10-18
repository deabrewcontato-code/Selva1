/* Selva1 · the plugin's window: the same panel as the web, in a web view, in its compact layout with tabs.
 * Copyright (C) 2026 DeAbrew. GPL-3.0-or-later. */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

class Selva1Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Selva1Editor(Selva1Processor&);
    ~Selva1Editor() override;
    void resized() override;
    void paint(juce::Graphics&) override;

    static constexpr int baseW = 1120, baseH = 720;

private:
    void timerCallback() override;
    void onPanel(const juce::var& v);
    void sendInit();
    std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& url);

    Selva1Processor& proc;
    juce::WebBrowserComponent web;
    bool ready = false;
    int seenState = -1;
    std::vector<float> sent;
    float scale = 1.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Selva1Editor)
};
