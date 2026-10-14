#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "SnareEngine.h"

/**
    HyperSnare : instrument VST3 de caisse claire en synthèse pure.
    MIDI : n'importe quelle note joue le même son ; seule la vélocité compte.
    Une frappe coupe la précédente.
*/
class HyperSnareAudioProcessor final : public juce::AudioProcessor
{
public:
    //==========================================================================
    // Identifiants des paramètres (ne pas les renommer : les projets DAW sauvegardés en dépendent)
    static constexpr const char* bodyId      = "body";
    static constexpr const char* balanceId   = "balance";
    static constexpr const char* characterId = "character";
    static constexpr const char* tuneId      = "tune";
    static constexpr const char* pitchId     = "pitch";
    static constexpr const char* colorId     = "color";
    static constexpr const char* snapId      = "snap";
    static constexpr const char* bodyDecayId = "bodyDecay";
    static constexpr const char* noiseDecayId = "noiseDecay";
    static constexpr const char* shapeId     = "shape";
    static constexpr const char* widthId     = "width";
    static constexpr const char* variationId = "variation";
    static constexpr const char* driveId     = "drive";
    static constexpr const char* crushId     = "crush";
    static constexpr const char* levelId     = "level";

    //==========================================================================
    /** Points de départ (les boutons 808 / 909 / HITECH / REAL). */
    struct Preset
    {
        const char* name;
        int body;
        float balance, character, tune, pitch, color, snap, bodyDecay, noiseDecay, shape, width, variation, drive, crush;
    };

    static const std::array<Preset, 4>& getPresets();
    void applyPreset (int index);
    /** Index du point de départ dont tous les réglages correspondent, sinon -1. */
    int getMatchingPreset() const;

    //==========================================================================
    HyperSnareAudioProcessor();
    ~HyperSnareAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "HyperSnare"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 4.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // Pour l'interface
    /** Demande une frappe d'écoute (bouton HIT de l'interface), jouée au prochain bloc. */
    void requestPreview() noexcept                          { previewRequest.store (1, std::memory_order_relaxed); }
    uint32_t getHitCount() const noexcept                   { return hitCount.load (std::memory_order_relaxed); }

    /** Réglages actuels du moteur, lus dans les paramètres. */
    hypersnare::SnareParams readParams() const;

    /** Texte d'une durée en ms ("45 ms", "1.20 s"). */
    static juce::String msToText (float ms);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void renderChunk (juce::AudioBuffer<float>&, int start, int length);

    hypersnare::SnareEngine engine;
    std::vector<float> scratchL, scratchR;

    std::atomic<float>* bodyParam = nullptr;
    std::atomic<float>* balanceParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* tuneParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* colorParam = nullptr;
    std::atomic<float>* snapParam = nullptr;
    std::atomic<float>* bodyDecayParam = nullptr;
    std::atomic<float>* noiseDecayParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* variationParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* crushParam = nullptr;
    std::atomic<float>* levelParam = nullptr;

    std::atomic<int> previewRequest { 0 };
    std::atomic<uint32_t> hitCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyperSnareAudioProcessor)
};
