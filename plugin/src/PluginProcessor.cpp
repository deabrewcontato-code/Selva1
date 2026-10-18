/* Selva1 · the plugin's audio side. Copyright (C) 2026 DeAbrew. GPL-3.0-or-later. */
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SelvaCore.h"
#include <cmath>
#include <mutex>

namespace
{
enum { K_LIN, K_EXP, K_INT, K_ENUM, K_BOOL, K_BIP };

/* "osc.a.level" -> "OSC A LEVEL": the name a host shows in its automation lanes */
juce::String hostName(const juce::String& key)
{
    auto s = key.replace(".", " ").toUpperCase();
    s = s.replace("FLT ", "FILTER ").replace("ENV", "ENV ").replace("LFO", "LFO ").replace("MOD", "MOD ");
    s = s.replace("MACRO", "MACRO ").replace("FX DIST", "DIST").replace("FX CHO", "CHORUS").replace("FX DLY", "DELAY");
    s = s.replace("FX REV", "REVERB").replace("FX COMP", "COMP").replace("  ", " ");
    return s.trim();
}

juce::File storeFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DeAbrew").getChildFile("Selva1").getChildFile("panel.json");
}
} // namespace

/* ---------------------------------------------------------------------------------------- description --- */
const Selva1Processor::Info& Selva1Processor::info()
{
    static const Info parsed = [] {
        Info in;
        s1_prepare();                              /* the shared tables, before any audio thread */
        auto json = juce::JSON::parse(juce::String::fromUTF8(s1_info()));
        for (auto& p : *json["params"].getArray())
        {
            Param q;
            q.key = p["key"].toString();
            q.name = hostName(q.key);
            q.kind = (int)p["kind"];
            q.min = (float)(double)p["min"];
            q.max = (float)(double)p["max"];
            q.def = (float)(double)p["def"];
            if (auto* o = p["opts"].getArray())
                for (auto& x : *o)
                    q.opts.add(x.toString());
            in.params.push_back(q);
        }
        for (auto& x : *json["sources"].getArray())
            in.sourceNames.add(x.toString());
        for (auto& d : *json["dests"].getArray())
            in.destNames.add(d["name"].toString());
        if (auto* ld = json["laneDefaults"].getArray())
            for (auto& x : *ld)
                in.laneDefaults.push_back((float)(double)x);
        in.nParams = (int)in.params.size();
        in.seqBase = (int)json["seqBase"];
        in.lanes = (int)json["lanes"];
        in.steps = (int)json["steps"];
        in.nAll = in.seqBase + in.lanes * in.steps;
        return in;
    }();
    return parsed;
}

juce::AudioProcessorValueTreeState::ParameterLayout Selva1Processor::makeLayout(const Info& in)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& p : in.params)
    {
        const juce::ParameterID id { p.key, 1 };
        const bool isSrc = p.key.startsWith("mod") && p.key.endsWith(".src");
        const bool isDst = p.key.startsWith("mod") && p.key.endsWith(".dst");
        if (p.kind == K_BOOL)
            layout.add(std::make_unique<juce::AudioParameterBool>(id, p.name, p.def > 0.5f));
        else if (isSrc || isDst)
        {
            auto names = isSrc ? in.sourceNames : in.destNames;
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, p.name, names, juce::roundToInt(p.def)));
        }
        else if (p.kind == K_ENUM && p.opts.size() > 0)
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, p.name, p.opts, juce::roundToInt(p.def)));
        else if (p.kind == K_INT || p.kind == K_ENUM)
            layout.add(std::make_unique<juce::AudioParameterInt>(id, p.name, (int)p.min, (int)p.max, juce::roundToInt(p.def)));
        else if (p.kind == K_EXP)
        {
            juce::NormalisableRange<float> r(
                p.min, p.max,
                [](float a, float b, float n) { return a * std::pow(b / a, n); },
                [](float a, float b, float v) { return std::log(juce::jmax(v, a) / a) / std::log(b / a); },
                [](float a, float b, float v) { return juce::jlimit(a, b, v); });
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, p.name, r, p.def));
        }
        else
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, p.name, juce::NormalisableRange<float>(p.min, p.max), p.def));
    }
    return layout;
}

/* ----------------------------------------------------------------------------------------- lifecycle --- */
Selva1Processor::Selva1Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      inf(info()),
      apvts(*this, nullptr, "SELVA1", makeLayout(info()))
{
    ctxMem.calloc((size_t)s1_ctx_size() + 64);
    ctx = (void*)(((uintptr_t)ctxMem.getData() + 63) & ~(uintptr_t)63);
    s1_bind(ctx);
    s1_init(48000.f);
    for (auto& p : inf.params)
        raw.push_back(apvts.getRawParameterValue(p.key));
    applied.assign((size_t)inf.nParams, std::nanf(""));
    steps.resize((size_t)(inf.lanes * inf.steps));
    for (int s = 0; s < inf.steps; s++)
        for (int l = 0; l < inf.lanes; l++)
            steps[(size_t)(s * inf.lanes + l)] = l < (int)inf.laneDefaults.size() ? inf.laneDefaults[(size_t)l] : 0.f;
}

Selva1Processor::~Selva1Processor() = default;

bool Selva1Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Selva1Processor::prepareToPlay(double sampleRate, int)
{
    s1_bind(ctx);
    s1_init((float)sampleRate);
    std::fill(applied.begin(), applied.end(), std::nanf(""));
    {
        const juce::ScopedLock sl(stateLock);
        for (size_t i = 0; i < steps.size(); i++)
            s1_set(inf.seqBase + (int)i, steps[i]);
        armedAudio = seqArmed;
    }
    freeRun = false;
    wasPlaying = false;
    expectPpq = -1;
}

/* ---------------------------------------------------------------------------------- panel -> audio --- */
void Selva1Processor::post(Msg m)
{
    const juce::SpinLock::ScopedLockType sl(postLock);   /* (several message-thread callers) */
    int s1, n1, s2, n2;
    fifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0)
        fifoBuf[(size_t)s1] = m;
    else if (n2 > 0)
        fifoBuf[(size_t)s2] = m;
    fifo.finishedWrite(n1 + n2);
}

void Selva1Processor::setValueFromUi(int id, float v)
{
    if (id < 0 || id >= inf.nAll)
        return;
    if (id < inf.nParams)
    {
        if (auto* p = apvts.getParameter(inf.params[(size_t)id].key))
            p->setValueNotifyingHost(p->convertTo0to1(v));
        return;
    }
    {
        const juce::ScopedLock sl(stateLock);
        steps[(size_t)(id - inf.seqBase)] = v;
    }
    post({ mSet, id, v });
}

void Selva1Processor::gesture(int id, bool begin)
{
    if (id < 0 || id >= inf.nParams)
        return;
    if (auto* p = apvts.getParameter(inf.params[(size_t)id].key))
        begin ? p->beginChangeGesture() : p->endChangeGesture();
}

void Selva1Processor::setArmed(bool on)
{
    {
        const juce::ScopedLock sl(stateLock);
        seqArmed = on;
    }
    post({ mArm, on ? 1 : 0, 0 });
}

juce::var Selva1Processor::allValues()
{
    juce::Array<juce::var> a;
    for (int i = 0; i < inf.nParams; i++)
        a.add(raw[(size_t)i]->load());
    const juce::ScopedLock sl(stateLock);
    for (auto v : steps)
        a.add(v);
    return a;
}

void Selva1Processor::handle(const Msg& m)
{
    switch (m.type)
    {
        case mSet: s1_set(m.a, m.b); break;
        case mNoteOn: s1_note_on(m.a, (int)m.b); break;
        case mNoteOff: s1_note_off(m.a); break;
        case mCC: s1_cc(m.a, (int)m.b); break;
        case mBend: s1_bend(m.b); break;
        case mAfter: s1_aftertouch(m.b); break;
        case mPanic: s1_panic(); s1_transport(0); break;
        case mArm:
            armedAudio = m.a != 0;
            /* armed with the host stopped: run now, on our own; a project being loaded only arms (b = 1) */
            freeRun = armedAudio && !wasPlaying && m.b < 0.5f;
            if (!armedAudio)
                s1_transport(0);
            else if (freeRun)
                s1_transport(1);
            break;
        default: break;
    }
}

/* ------------------------------------------------------------------------------------------ audio --- */
void Selva1Processor::renderSlice(float* l, float* r, int n)
{
    if (n > 0)
        s1_render(l, r, n);
}

void Selva1Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    s1_bind(ctx);

    /* the parameters the host or the panel changed */
    for (int i = 0; i < inf.nParams; i++)
    {
        const float v = raw[(size_t)i]->load(std::memory_order_relaxed);
        if (!(v == applied[(size_t)i]))
        {
            s1_set(i, v);
            applied[(size_t)i] = v;
        }
    }
    /* the panel's messages */
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead(fifo.getNumReady(), s1, n1, s2, n2);
        for (int k = 0; k < n1; k++)
            handle(fifoBuf[(size_t)(s1 + k)]);
        for (int k = 0; k < n2; k++)
            handle(fifoBuf[(size_t)(s2 + k)]);
        fifo.finishedRead(n1 + n2);
    }
    /* the host's tempo and transport: the sequencer follows them when SEQ is on */
    bool playing = false;
    double ppq = -1, bpm = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm())
                bpm = *b;
            if (auto q = pos->getPpqPosition())
                ppq = *q;
        }
    s1_host_tempo((float)bpm);
    hostPlaying.store(playing);
    hostBpm.store(bpm);
    if (armedAudio)
    {
        if (playing && (!wasPlaying || (ppq >= 0 && expectPpq >= 0 && std::abs(ppq - expectPpq) > 0.05)))
        {
            s1_transport_from(ppq >= 0 ? ppq : 0);   /* start, or the host jumped (a loop, a locate) */
            freeRun = false;
        }
        else if (!playing && wasPlaying && !freeRun)
            s1_transport(0);
    }
    wasPlaying = playing;
    expectPpq = (playing && ppq >= 0 && bpm > 0) ? ppq + n / getSampleRate() * bpm / 60.0 : -1;

    /* MIDI, sample-accurate: render up to each event */
    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    int at = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit(0, n, meta.samplePosition);
        renderSlice(l + at, r + at, t - at);
        at = t;
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            s1_note_on(msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            s1_note_off(msg.getNoteNumber());
        else if (msg.isPitchWheel())
            s1_bend((msg.getPitchWheelValue() - 8192) / 8192.f);
        else if (msg.isChannelPressure())
            s1_aftertouch(msg.getChannelPressureValue() / 127.f);
        else if (msg.isController())
            s1_cc(msg.getControllerNumber(), msg.getControllerValue());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            s1_panic();
    }
    renderSlice(l + at, r + at, n - at);
    for (int c = 2; c < buffer.getNumChannels(); c++)
        buffer.clear(c, 0, n);

    s1_ui_state(tmp);
    for (int k = 0; k < 14; k++)
        uiState[(size_t)k].store(tmp[k], std::memory_order_relaxed);
}

/* ------------------------------------------------------------------------------------------ state --- */
void Selva1Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("v", 2);
    auto* p = new juce::DynamicObject();
    for (int i = 0; i < inf.nParams; i++)
        p->setProperty(inf.params[(size_t)i].key, raw[(size_t)i]->load());
    o->setProperty("p", juce::var(p));
    {
        const juce::ScopedLock sl(stateLock);
        juce::Array<juce::var> s;
        for (auto v : steps)
            s.add(v);
        o->setProperty("s", s);
        o->setProperty("name", presetName);
        o->setProperty("armed", seqArmed);
    }
    destData.reset();
    juce::MemoryOutputStream(destData, false).writeString(juce::JSON::toString(juce::var(o), true));
}

void Selva1Processor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto text = juce::String::fromUTF8((const char*)data, sizeInBytes);
    auto st = juce::JSON::parse(text.trimCharactersAtEnd(juce::String::charToString(0)));
    if (!st.isObject())
        return;
    if (auto* p = st["p"].getDynamicObject())
        for (int i = 0; i < inf.nParams; i++)
        {
            const auto& q = inf.params[(size_t)i];
            const auto v = p->getProperty(q.key);
            if (auto* prm = apvts.getParameter(q.key))
                prm->setValueNotifyingHost(prm->convertTo0to1(v.isVoid() ? q.def : (float)(double)v));
        }
    {
        const juce::ScopedLock sl(stateLock);
        if (auto* s = st["s"].getArray())
            for (int i = 0; i < juce::jmin((int)steps.size(), s->size()); i++)
                steps[(size_t)i] = (float)(double)(*s)[i];
        presetName = st["name"].toString().isEmpty() ? juce::String("INIT") : st["name"].toString();
        seqArmed = (bool)st["armed"];
    }
    {
        const juce::ScopedLock sl(stateLock);
        for (size_t i = 0; i < steps.size(); i++)
            post({ mSet, inf.seqBase + (int)i, steps[i] });
    }
    post({ mArm, seqArmed ? 1 : 0, 1.f });
    stateVersion++;
}

/* ------------------------------------------------------------------------------------ panel store --- */
juce::var Selva1Processor::loadStore()
{
    auto v = juce::JSON::parse(storeFile());
    return v.isObject() ? v : juce::var(new juce::DynamicObject());
}

void Selva1Processor::saveStoreKey(const juce::String& key, const juce::String& value)
{
    static std::mutex m;
    const std::lock_guard<std::mutex> g(m);
    auto v = loadStore();
    if (auto* o = v.getDynamicObject())
        o->setProperty(key, value);
    auto f = storeFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::JSON::toString(v, true));
}

juce::AudioProcessorEditor* Selva1Processor::createEditor() { return new Selva1Editor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Selva1Processor(); }
