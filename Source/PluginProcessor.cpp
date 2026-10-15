#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float textToNumber (const juce::String& text)
    {
        return text.retainCharacters ("-0123456789.,").replaceCharacter (',', '.').getFloatValue();
    }

    float textToTime (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
        return (t.endsWithChar ('s') && ! t.endsWith ("ms")) ? value * 1000.0f : value;
    }

    juce::String kiloHertzToText (float hertz, int)
    {
        return juce::String (hertz / 1000.0f, 1) + " kHz";
    }

    float textToFrequency (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.retainCharacters ("0123456789.,").replaceCharacter (',', '.').getFloatValue();
        return t.containsChar ('k') ? value * 1000.0f : value;
    }

    /** Plage exponentielle (temps, fréquences) : le milieu de la course est la moyenne géométrique. */
    juce::NormalisableRange<float> makeLogRange (float lowest, float highest, float step)
    {
        return { lowest, highest,
                 [] (float start, float end, float normalised)
                 {
                     return start * std::pow (end / start, juce::jlimit (0.0f, 1.0f, normalised));
                 },
                 [] (float start, float end, float value)
                 {
                     return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (start, end, value) / start) / std::log (end / start));
                 },
                 [step] (float start, float end, float value)
                 {
                     return juce::jlimit (start, end, std::round (value / step) * step);
                 } };
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercent (const char* id, const char* name, float defaultPercent)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), defaultPercent,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; })
                                                                .withValueFromStringFunction (textToNumber));
    }

    std::unique_ptr<juce::AudioParameterFloat> makeTime (const char* id, const char* name, float lowest, float highest, float defaultMs)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                            makeLogRange (lowest, highest, 0.1f), defaultMs,
                                                            juce::AudioParameterFloatAttributes()
                                                                .withStringFromValueFunction ([] (float ms, int) { return HyperSnareAudioProcessor::msToText (ms); })
                                                                .withValueFromStringFunction (textToTime));
    }
}

//==============================================================================
const std::array<HyperSnareAudioProcessor::Preset, 4>& HyperSnareAudioProcessor::getPresets()
{
    //                                       body                     bal  char tune    pitch color    snap  bodyD noiseD shape width var drive crush
    static const std::array<Preset, 4> presets { {
        { "808",    hypersnare::bodySine,     45,  10,  190.0f,  3.0f, 5500.0f, 30,  160,  220,   0,    15,   10, 10,   0 },
        { "909",    hypersnare::bodySine,     55,  25,  220.0f,  7.0f, 7000.0f, 65,  120,  260,   15,   25,   15, 20,   0 },
        { "HITECH", hypersnare::bodyFm,       50,  30,  240.0f, 14.0f, 6500.0f, 80,  90,   140,   65,   35,   20, 40,   0 },
        { "REAL",   hypersnare::bodyMembrane, 55,  90,  200.0f,  2.0f, 4500.0f, 55,  220,  380,   5,    45,   40, 15,   0 }
    } };

    return presets;
}

namespace
{
    template <typename Fn>
    void forEachPresetValue (const HyperSnareAudioProcessor::Preset& p, Fn&& fn)
    {
        using P = HyperSnareAudioProcessor;
        fn (P::bodyId, (float) p.body);
        fn (P::balanceId, p.balance);
        fn (P::characterId, p.character);
        fn (P::tuneId, p.tune);
        fn (P::pitchId, p.pitch);
        fn (P::colorId, p.color);
        fn (P::snapId, p.snap);
        fn (P::bodyDecayId, p.bodyDecay);
        fn (P::noiseDecayId, p.noiseDecay);
        fn (P::shapeId, p.shape);
        fn (P::widthId, p.width);
        fn (P::variationId, p.variation);
        fn (P::driveId, p.drive);
        fn (P::crushId, p.crush);
    }
}

void HyperSnareAudioProcessor::applyPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) getPresets().size()))
        return;

    forEachPresetValue (getPresets()[(size_t) index], [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
    });
}

int HyperSnareAudioProcessor::getMatchingPreset() const
{
    for (int i = 0; i < (int) getPresets().size(); ++i)
    {
        bool same = true;

        forEachPresetValue (getPresets()[(size_t) i], [this, &same] (const char* id, float value)
        {
            if (auto* param = apvts.getParameter (id))
                if (std::abs (param->getValue() - param->convertTo0to1 (value)) > 0.002f)
                    same = false;
        });

        if (same)
            return i;
    }

    return -1;
}

//==============================================================================
juce::String HyperSnareAudioProcessor::msToText (float ms)
{
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 2) + " s";

    return juce::String (juce::roundToInt (ms)) + " ms";
}

juce::AudioProcessorValueTreeState::ParameterLayout HyperSnareAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bodyId, 1 }, "Body",
                                                              juce::StringArray { "Sine", "Membrane", "FM" }, hypersnare::bodyFm));
    layout.add (makePercent (balanceId, "Balance (body / noise)", 50.0f));
    layout.add (makePercent (characterId, "Character (synth / acoustic)", 30.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { tuneId, 1 }, "Tune",
                                                             makeLogRange (100.0f, 400.0f, 1.0f), 240.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float hz, int) { return juce::String (juce::roundToInt (hz)) + " Hz"; })
                                                                 .withValueFromStringFunction (textToFrequency)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pitchId, 1 }, "Pitch drop",
                                                             juce::NormalisableRange<float> (0.0f, 36.0f, 0.1f, 0.6f), 14.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float st, int)
                                                                 {
                                                                     const bool whole = std::abs (st - std::round (st)) < 0.05f;
                                                                     return (whole ? juce::String (juce::roundToInt (st)) : juce::String (st, 1)) + " st";
                                                                 })
                                                                 .withValueFromStringFunction (textToNumber)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { colorId, 1 }, "Color",
                                                             makeLogRange (1000.0f, 12000.0f, 1.0f), 6500.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (kiloHertzToText)
                                                                 .withValueFromStringFunction ([] (const juce::String& text)
                                                                 {
                                                                     const auto v = textToFrequency (text);
                                                                     return v < 100.0f ? v * 1000.0f : v;
                                                                 })));

    layout.add (makePercent (snapId, "Snap", 80.0f));
    layout.add (makeTime (bodyDecayId, "Body decay", 20.0f, 800.0f, 90.0f));
    layout.add (makeTime (noiseDecayId, "Noise decay", 20.0f, 1500.0f, 140.0f));
    layout.add (makePercent (shapeId, "Shape (natural / gated)", 65.0f));
    layout.add (makePercent (widthId, "Width", 35.0f));
    layout.add (makePercent (variationId, "Variation", 20.0f));
    layout.add (makePercent (driveId, "Drive", 40.0f));
    layout.add (makePercent (crushId, "Crush", 0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { levelId, 1 }, "Level",
                                                             juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction ([] (float db, int)
                                                                 {
                                                                     return (db > 0.04f ? "+" : "") + juce::String (db, 1) + " dB";
                                                                 })
                                                                 .withValueFromStringFunction (textToNumber)));
    return layout;
}

//==============================================================================
HyperSnareAudioProcessor::HyperSnareAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HyperSnare", createParameterLayout()),
      presets (apvts, "HyperSnare", ".hspreset",
               { [] { return (int) getPresets().size(); },
                 [] (int i) { return juce::String (getPresets()[(size_t) i].name); },
                 [this] (int i) { applyPreset (i); },
                 [this] { return getMatchingPreset(); } })
{
    bodyParam       = apvts.getRawParameterValue (bodyId);
    balanceParam    = apvts.getRawParameterValue (balanceId);
    characterParam  = apvts.getRawParameterValue (characterId);
    tuneParam       = apvts.getRawParameterValue (tuneId);
    pitchParam      = apvts.getRawParameterValue (pitchId);
    colorParam      = apvts.getRawParameterValue (colorId);
    snapParam       = apvts.getRawParameterValue (snapId);
    bodyDecayParam  = apvts.getRawParameterValue (bodyDecayId);
    noiseDecayParam = apvts.getRawParameterValue (noiseDecayId);
    shapeParam      = apvts.getRawParameterValue (shapeId);
    widthParam      = apvts.getRawParameterValue (widthId);
    variationParam  = apvts.getRawParameterValue (variationId);
    driveParam      = apvts.getRawParameterValue (driveId);
    crushParam      = apvts.getRawParameterValue (crushId);
    levelParam      = apvts.getRawParameterValue (levelId);

    engine.setParams (readParams());
    presets.restoreFromState();   // réglages par défaut = HITECH
}

hypersnare::SnareParams HyperSnareAudioProcessor::readParams() const
{
    hypersnare::SnareParams p;
    p.body      = juce::jlimit (0, 2, juce::roundToInt (bodyParam->load()));
    p.balance   = balanceParam->load() * 0.01f;
    p.character = characterParam->load() * 0.01f;
    p.tuneHz    = tuneParam->load();
    p.pitch     = pitchParam->load();
    p.colorHz   = colorParam->load();
    p.snap      = snapParam->load() * 0.01f;
    p.bodyMs    = bodyDecayParam->load();
    p.noiseMs   = noiseDecayParam->load();
    p.shape     = shapeParam->load() * 0.01f;
    p.width     = widthParam->load() * 0.01f;
    p.variation = variationParam->load() * 0.01f;
    p.drive     = driveParam->load() * 0.01f;
    p.crush     = crushParam->load() * 0.01f;
    p.levelDb   = levelParam->load();
    return p;
}

void HyperSnareAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto size = (size_t) juce::jmax (512, samplesPerBlock);
    scratchL.assign (size, 0.0f);
    scratchR.assign (size, 0.0f);

    engine.setParams (readParams());
    engine.prepare (sampleRate);
}

void HyperSnareAudioProcessor::reset()
{
    engine.reset();
}

bool HyperSnareAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() > 0 && ! layouts.getMainInputChannelSet().isDisabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void HyperSnareAudioProcessor::renderChunk (juce::AudioBuffer<float>& buffer, int start, int length)
{
    const int channels = buffer.getNumChannels();

    while (length > 0)
    {
        const int n = juce::jmin (length, (int) scratchL.size());
        engine.process (scratchL.data(), scratchR.data(), n);

        if (channels >= 2)
        {
            buffer.copyFrom (0, start, scratchL.data(), n);
            buffer.copyFrom (1, start, scratchR.data(), n);
        }
        else if (channels == 1)
        {
            auto* out = buffer.getWritePointer (0, start);
            for (int i = 0; i < n; ++i)
                out[i] = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
        }

        start += n;
        length -= n;
    }
}

void HyperSnareAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    if (scratchL.empty())   // l'hôte n'a pas appelé prepareToPlay
        return;

    engine.setParams (readParams());

    if (previewRequest.exchange (0, std::memory_order_relaxed) != 0)
        engine.trigger (0.9f);

    int position = 0;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const int at = juce::jlimit (position, numSamples, metadata.samplePosition);

        if (message.isNoteOn())
        {
            renderChunk (buffer, position, at - position);
            position = at;
            engine.trigger (message.getFloatVelocity());   // la hauteur de la note est ignorée
        }
        else if (message.isAllSoundOff())
        {
            renderChunk (buffer, position, at - position);
            position = at;
            engine.reset();
        }
    }

    renderChunk (buffer, position, numSamples - position);

    hitCount.store (engine.getHitCount(), std::memory_order_relaxed);
}

void HyperSnareAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();
    engine.reset();
}

//==============================================================================
void HyperSnareAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HyperSnareAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.restoreFromState();
        }
}

juce::AudioProcessorEditor* HyperSnareAudioProcessor::createEditor()
{
    return new HyperSnareAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HyperSnareAudioProcessor();
}
