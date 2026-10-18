/* Selva1 · the plugin's window. Copyright (C) 2026 DeAbrew. GPL-3.0-or-later. */
#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
juce::WebBrowserComponent::Options webOptions(Selva1Editor* ed, std::function<void(const juce::var&)> onEvent,
                                              std::function<std::optional<juce::WebBrowserComponent::Resource>(const juce::String&)> res)
{
    juce::ignoreUnused(ed);
    return juce::WebBrowserComponent::Options {}
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2 {}
                                    .withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                            .getChildFile("Selva1WebView2"))
                                    .withBackgroundColour(juce::Colour(0xff141414)))
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withUserScript("window.SELVA1_HOST = 'plugin';")
        .withEventListener("s1", std::move(onEvent))
        .withResourceProvider(std::move(res));
}
} // namespace

Selva1Editor::Selva1Editor(Selva1Processor& p)
    : AudioProcessorEditor(&p), proc(p),
      web(webOptions(this, [this](const juce::var& v) { onPanel(v); },
                     [this](const juce::String& url) { return resource(url); }))
{
    sent.assign((size_t)proc.inf.nParams, std::nanf(""));
    addAndMakeVisible(web);
    web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    if (auto s = proc.loadStore()["scale"]; !s.isVoid())
        scale = juce::jlimit(0.6f, 2.f, (float)s.toString().getDoubleValue());
    setSize(juce::roundToInt(baseW * scale), juce::roundToInt(baseH * scale));
    startTimerHz(30);
}

Selva1Editor::~Selva1Editor() { stopTimer(); }

void Selva1Editor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff141414)); }
void Selva1Editor::resized() { web.setBounds(getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> Selva1Editor::resource(const juce::String& url)
{
    const auto path = url.fromFirstOccurrenceOf(juce::WebBrowserComponent::getResourceProviderRoot(), false, false);
    if (path.isEmpty() || path == "index.html" || path.startsWith("?") || path.startsWith("#"))
    {
        const auto* d = reinterpret_cast<const std::byte*>(BinaryData::index_html);
        return juce::WebBrowserComponent::Resource { std::vector<std::byte>(d, d + BinaryData::index_htmlSize), "text/html" };
    }
    return std::nullopt;
}

void Selva1Editor::sendInit()
{
    auto* o = new juce::DynamicObject();
    o->setProperty("t", "init");
    o->setProperty("vals", proc.allValues());
    {
        const juce::ScopedLock sl(proc.stateLock);
        o->setProperty("name", proc.presetName);
        o->setProperty("armed", proc.seqArmed);
    }
    o->setProperty("kv", proc.loadStore());
    o->setProperty("format", juce::AudioProcessor::getWrapperTypeDescription(proc.wrapperType));
    o->setProperty("scale", scale);
    web.emitEventIfBrowserIsVisible("s1", juce::var(o));
    for (int i = 0; i < proc.inf.nParams; i++)
        sent[(size_t)i] = proc.apvts.getRawParameterValue(proc.inf.params[(size_t)i].key)->load();
    seenState = proc.stateVersion.load();
}

void Selva1Editor::onPanel(const juce::var& v)
{
    const auto t = v["t"].toString();
    if (t == "ready")
    {
        ready = true;
        sendInit();
    }
    else if (t == "set")
    {
        if (auto* a = v["a"].getArray())
            for (int i = 0; i + 1 < a->size(); i += 2)
            {
                const int id = (int)(*a)[i];
                const float x = (float)(double)(*a)[i + 1];
                if (id >= 0 && id < proc.inf.nParams)
                    sent[(size_t)id] = x;          /* (no echo back to the panel) */
                proc.setValueFromUi(id, x);
            }
    }
    else if (t == "g")
        proc.gesture((int)v["id"], (bool)v["s"]);
    else if (t == "on")
        proc.post({ Selva1Processor::mNoteOn, (int)v["n"], (float)(double)v["v"] });
    else if (t == "off")
        proc.post({ Selva1Processor::mNoteOff, (int)v["n"], 0 });
    else if (t == "cc")
        proc.post({ Selva1Processor::mCC, (int)v["c"], (float)(double)v["v"] });
    else if (t == "bend")
        proc.post({ Selva1Processor::mBend, 0, (float)(double)v["v"] });
    else if (t == "at")
        proc.post({ Selva1Processor::mAfter, 0, (float)(double)v["v"] });
    else if (t == "panic")
        proc.post({ Selva1Processor::mPanic, 0, 0 });
    else if (t == "play")
        proc.setArmed((bool)v["v"]);
    else if (t == "name")
    {
        const juce::ScopedLock sl(proc.stateLock);
        proc.presetName = v["n"].toString();
    }
    else if (t == "kv")
        proc.saveStoreKey(v["k"].toString(), v["v"].toString());
    else if (t == "scale")
    {
        scale = juce::jlimit(0.6f, 2.f, (float)(double)v["v"]);
        proc.saveStoreKey("scale", juce::String(scale));
        setSize(juce::roundToInt(baseW * scale), juce::roundToInt(baseH * scale));
    }
}

void Selva1Editor::timerCallback()
{
    if (!ready)
        return;
    if (proc.stateVersion.load() != seenState)
    {
        sendInit();
        return;
    }
    juce::Array<juce::var> s;
    for (int k = 0; k < 14; k++)
        s.add(proc.uiState[(size_t)k].load());
    auto* o = new juce::DynamicObject();
    o->setProperty("t", "ui");
    o->setProperty("s", s);
    o->setProperty("play", proc.hostPlaying.load());
    o->setProperty("bpm", proc.hostBpm.load());
    web.emitEventIfBrowserIsVisible("s1", juce::var(o));
    juce::Array<juce::var> changed;
    for (int i = 0; i < proc.inf.nParams; i++)
    {
        const float x = proc.apvts.getRawParameterValue(proc.inf.params[(size_t)i].key)->load();
        if (!(x == sent[(size_t)i]))
        {
            sent[(size_t)i] = x;
            changed.add(i);
            changed.add(x);
        }
    }
    if (!changed.isEmpty())
    {
        auto* c = new juce::DynamicObject();
        c->setProperty("t", "vals");
        c->setProperty("a", changed);
        web.emitEventIfBrowserIsVisible("s1", juce::var(c));
    }
}
