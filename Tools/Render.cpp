// Rendu hors ligne : extraits audio (WAV 24 bits) et capture de l'interface (PNG).
// Usage : HyperSnareRender <dossier de sortie>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double bpm = 190.0;

    struct Setting { const char* id; float value; };

    void apply (HyperSnareAudioProcessor& p, std::initializer_list<Setting> settings)
    {
        for (auto& s : settings)
        {
            auto* param = p.apvts.getParameter (s.id);
            param->setValueNotifyingHost (param->convertTo0to1 (s.value));
        }
    }

    /** 2 mesures : snare sur les temps 2 et 4, une ghost note, puis un roll en double- et triple-croches qui monte en vélocité. */
    juce::MidiBuffer pattern (int& lengthOut)
    {
        juce::MidiBuffer midi;
        const double beat = 60.0 / bpm * sampleRate;
        auto add = [&] (double beats, float velocity) { midi.addEvent (juce::MidiMessage::noteOn (1, 38, velocity), juce::roundToInt (beats * beat)); };

        add (1.0, 1.0f);
        add (2.75, 0.35f);
        add (3.0, 1.0f);
        add (5.0, 1.0f);

        for (int i = 0; i < 8; ++i)                 // double-croches, temps 7 et 8
            add (6.0 + i * 0.25, 0.45f + 0.04f * (float) i);
        for (int i = 0; i < 8; ++i)                 // triples-croches, temps 9 et 10
            add (8.0 + i * 0.125, 0.75f + 0.03f * (float) i);

        lengthOut = juce::roundToInt (9.5 * beat + sampleRate * 0.8);
        return midi;
    }

    void renderTo (HyperSnareAudioProcessor& p, const juce::File& file)
    {
        p.setRateAndBufferSizeDetails (sampleRate, 512);
        p.prepareToPlay (sampleRate, 512);

        int length = 0;
        auto midi = pattern (length);
        juce::AudioBuffer<float> out (2, length);
        out.clear();
        juce::AudioBuffer<float> block (2, 512);

        for (int pos = 0; pos < length; pos += 512)
        {
            const int n = juce::jmin (512, length - pos);
            block.setSize (2, n, false, false, true);
            juce::MidiBuffer part;
            for (const auto m : midi)
                if (m.samplePosition >= pos && m.samplePosition < pos + n)
                    part.addEvent (m.getMessage(), m.samplePosition - pos);

            p.processBlock (block, part);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, block, ch, 0, n);
        }

        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (out, 0, length);

        std::printf ("%s : crête %.1f dBFS\n", file.getFileName().toRawUTF8(), juce::Decibels::gainToDecibels (out.getMagnitude (0, length)));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    dir.createDirectory();

    using P = HyperSnareAudioProcessor;
    const char* names[4] = { "01-808", "02-909", "03-hitech", "04-real" };

    for (int i = 0; i < 4; ++i)
    {
        P p;
        p.applyPreset (i);
        renderTo (p, dir.getChildFile (juce::String (names[i]) + ".wav"));
    }

    {
        P p;
        p.applyPreset (2);
        apply (p, { { P::pitchId, 30.0f }, { P::bodyDecayId, 60.0f }, { P::noiseDecayId, 90.0f }, { P::driveId, 75.0f }, { P::snapId, 100.0f } });
        renderTo (p, dir.getChildFile ("05-zap.wav"));
    }

    {
        P p;
        p.applyPreset (2);
        apply (p, { { P::bodyId, 1.0f }, { P::characterId, 60.0f }, { P::noiseDecayId, 600.0f }, { P::shapeId, 0.0f }, { P::crushId, 45.0f } });
        renderTo (p, dir.getChildFile ("06-dark-crush.wav"));
    }

    {
        P p;
        p.setRateAndBufferSizeDetails (sampleRate, 512);
        p.prepareToPlay (sampleRate, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto file = dir.getChildFile ("interface.png");
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            png.writeImageToStream (image, *stream);
        p.editorBeingDeleted (editor.get());
    }

    return 0;
}
