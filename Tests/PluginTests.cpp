// Tests du plugin complet (paramètres, MIDI, état, interface).

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cstdio>

#define PRESET_EXT ".hspreset"
#define PLUGIN_NAME "HyperSnare"
#define EDITOR_CLASS HyperSnareAudioProcessorEditor
#define P_A_ID HyperSnareAudioProcessor::tuneId
#define P_A_VALUE 260.0f
#define P_A_OTHER 150.0f
#define P_LEVEL_ID HyperSnareAudioProcessor::levelId

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (%s:%d) ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct Note { int at; int number; float velocity; };

    juce::AudioBuffer<float> run (HyperSnareAudioProcessor& p, int samples, const std::vector<Note>& notes, int block = blockSize, int channels = 2)
    {
        juce::AudioBuffer<float> out (channels, samples);
        out.clear();
        juce::AudioBuffer<float> buffer (channels, block);

        for (int pos = 0; pos < samples; pos += block)
        {
            const int n = juce::jmin (block, samples - pos);
            buffer.setSize (channels, n, false, false, true);
            juce::MidiBuffer midi;

            for (auto& note : notes)
                if (note.at >= pos && note.at < pos + n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note.number, note.velocity), note.at - pos);

            p.processBlock (buffer, midi);

            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }

        return out;
    }

    bool isSilent (const juce::AudioBuffer<float>& b, int from, int count)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = from; i < from + count; ++i)
                if (b.getSample (ch, i) != 0.0f)
                    return false;
        return true;
    }

    bool sameAudio (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
            return false;

        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
                return false;
        return true;
    }

    void setParam (HyperSnareAudioProcessor& p, const char* id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void prepare (HyperSnareAudioProcessor& p)
    {
        p.setRateAndBufferSizeDetails (sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    using P = HyperSnareAudioProcessor;

    std::printf ("Paramètres\n");
    {
        P p;
        CHECK (p.getParameters().size() == 15, "%d paramètres au lieu de 15", p.getParameters().size());

        for (auto* id : { P::bodyId, P::balanceId, P::characterId, P::tuneId, P::pitchId, P::colorId, P::snapId,
                          P::bodyDecayId, P::noiseDecayId, P::shapeId, P::widthId, P::variationId, P::driveId, P::crushId, P::levelId })
            CHECK (p.apvts.getParameter (id) != nullptr, "paramètre %s absent", id);

        CHECK (p.getMatchingPreset() == 2, "le réglage par défaut devrait être HITECH (%d)", p.getMatchingPreset());

        for (int i = 0; i < 4; ++i)
        {
            p.applyPreset (i);
            CHECK (p.getMatchingPreset() == i, "point de départ %d non reconnu après application", i);
        }

        auto* tune = p.apvts.getParameter (P::tuneId);
        CHECK (tune->getText (tune->convertTo0to1 (240.0f), 32) == "240 Hz", "texte de TUNE : %s", tune->getText (tune->convertTo0to1 (240.0f), 32).toRawUTF8());
        auto* color = p.apvts.getParameter (P::colorId);
        CHECK (color->getText (color->convertTo0to1 (6500.0f), 32) == "6.5 kHz", "texte de COLOR");
        CHECK (std::abs (color->convertFrom0to1 (color->getValueForText ("8k")) - 8000.0f) < 2.0f, "saisie 8k");
        auto* noise = p.apvts.getParameter (P::noiseDecayId);
        CHECK (noise->getText (noise->convertTo0to1 (1200.0f), 32) == "1.20 s", "texte de NOISE");
        auto* pitch = p.apvts.getParameter (P::pitchId);
        CHECK (pitch->getText (pitch->convertTo0to1 (14.0f), 32) == "14 st", "texte de PITCH : %s", pitch->getText (pitch->convertTo0to1 (14.0f), 32).toRawUTF8());
    }

    std::printf ("MIDI : toutes les notes jouent le même son\n");
    {
        P base;
        setParam (base, P::variationId, 0.0f);
        juce::MemoryBlock state;
        base.getStateInformation (state);

        juce::AudioBuffer<float> reference;
        for (int note : { 0, 38, 40, 60, 64, 96, 127 })
        {
            P p;
            p.setStateInformation (state.getData(), (int) state.getSize());
            prepare (p);
            auto out = run (p, 24000, { { 100, note, 0.8f } });

            if (reference.getNumSamples() == 0)
                reference = out;
            else
                CHECK (sameAudio (out, reference), "la note %d sonne différemment", note);
        }

        P p;
        prepare (p);
        CHECK (isSilent (run (p, 4800, {}), 0, 4800), "son sans note");

        p.reset();
        auto exact = run (p, 2048, { { 700, 72, 1.0f } });
        int first = -1;
        for (int i = 0; i < 2048; ++i) if (exact.getSample (0, i) != 0.0f) { first = i; break; }
        CHECK (first >= 700 && first <= 702, "première valeur non nulle à %d (attendu 700)", first);

        p.reset();
        CHECK (isSilent (run (p, 4800, { { 100, 64, 0.0f } }), 0, 4800), "une vélocité nulle déclenche une frappe");
    }

    std::printf ("Écoute depuis l'interface, coupure, contournement, sortie mono, blocs variables\n");
    {
        P p;
        prepare (p);

        const auto before = p.getHitCount();
        p.requestPreview();
        auto preview = run (p, 4800, {});
        CHECK (p.getHitCount() == before + 1, "la frappe d'écoute n'a pas joué");
        CHECK (preview.getMagnitude (0, 4800) > 0.05f, "frappe d'écoute silencieuse");

        p.reset();
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 70, 1.0f), 0);
        p.processBlock (b, m);
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allSoundOff (1), 10);
        p.processBlock (b, off);
        CHECK (isSilent (b, 20, 492), "All Sound Off n'arrête pas le son");

        p.reset();
        juce::MidiBuffer hit;
        hit.addEvent (juce::MidiMessage::noteOn (1, 70, 1.0f), 0);
        b.clear();
        p.processBlockBypassed (b, hit);
        CHECK (isSilent (b, 0, 512), "son en contournement");

        p.reset();
        auto big = run (p, 20000, { { 5000, 40, 1.0f }, { 15000, 70, 1.0f } }, 8192);
        CHECK (isSilent (big, 0, 5000) && big.getMagnitude (5000, 2000) > 0.05f && big.getMagnitude (15000, 2000) > 0.05f, "blocs de 8192 mal gérés");

        P mono;
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (mono.setBusesLayout (layout), "sortie mono refusée");
        prepare (mono);
        CHECK (run (mono, 4800, { { 0, 70, 1.0f } }, blockSize, 1).getMagnitude (0, 4800) > 0.05f, "sortie mono silencieuse");

        juce::AudioProcessor::BusesLayout withInput;
        withInput.inputBuses.add (juce::AudioChannelSet::stereo());
        withInput.outputBuses.add (juce::AudioChannelSet::stereo());
        CHECK (! p.checkBusesLayoutSupported (withInput), "une entrée audio est acceptée");
    }

    std::printf ("Sauvegarde et rechargement\n");
    {
        P a;
        setParam (a, P::bodyId, 1.0f);
        setParam (a, P::tuneId, 312.0f);
        setParam (a, P::noiseDecayId, 777.0f);
        setParam (a, P::levelId, -7.5f);
        a.apvts.state.setProperty ("editorWidth", 850, nullptr);

        juce::MemoryBlock state;
        a.getStateInformation (state);

        P b;
        b.setStateInformation (state.getData(), (int) state.getSize());

        for (auto* param : a.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);
            auto* other = b.apvts.getParameter (withId->paramID);
            CHECK (std::abs (param->getValue() - other->getValue()) < 1e-6f, "%s non restauré", withId->paramID.toRawUTF8());
        }

        CHECK ((int) b.apvts.state.getProperty ("editorWidth") == 850, "taille de fenêtre non restaurée");

        const char garbage[] = "not a state at all";
        b.setStateInformation (garbage, (int) sizeof (garbage));
        b.setStateInformation (nullptr, 0);
        juce::MemoryBlock truncated (state.getData(), state.getSize() / 2);
        b.setStateInformation (truncated.getData(), (int) truncated.getSize());
        CHECK (std::abs (b.apvts.getRawParameterValue (P::levelId)->load() + 7.5f) < 1e-4f, "un état abîmé a changé les réglages");
    }

    std::printf ("Presets utilisateur\n");
    {
        using Proc = HyperSnareAudioProcessor;
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PresetTests-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        dir.deleteRecursively();
        const auto ext = juce::String (PRESET_EXT);

        Proc p;
        p.presets.setFolderOverride (dir);
        const int factoryCount = (int) Proc::getPresets().size();

        // Nouvelle instance : point de départ par défaut reconnu, pas d'étoile
        CHECK (! p.presets.getCurrent().user && p.presets.getCurrent().factoryIndex == p.getMatchingPreset() && p.getMatchingPreset() >= 0,
               "nouvelle instance : point de départ par défaut non reconnu");
        CHECK (! p.presets.isModified(), "nouvelle instance marquée modifiée");
        CHECK (p.presets.getUserPresets().isEmpty(), "dossier de test pas vide");

        // Noms
        CHECK (PresetLibrary::cleanName ("  a/b:c  ").isNotEmpty() && ! PresetLibrary::cleanName ("a/b:c").containsAnyOf ("/:"), "nom mal nettoyé");
        CHECK (PresetLibrary::cleanName ("   ").isEmpty() && PresetLibrary::cleanName ("...").isEmpty(), "nom vide accepté");
        CHECK (! p.presets.save ("   "), "un nom vide a été enregistré");
        CHECK (PresetLibrary::cleanName (juce::String::repeatedString ("x", 200)).length() <= 48, "nom trop long");

        // Enregistrer : fichier créé, preset en cours, pas d'étoile
        setParam (p, P_A_ID, P_A_VALUE);
        setParam (p, P_LEVEL_ID, -7.5f);
        const float aBefore = p.apvts.getRawParameterValue (P_A_ID)->load();
        CHECK (p.presets.save ("Darkpsy Crack"), "enregistrement impossible");
        const auto file = dir.getChildFile ("Darkpsy Crack" + ext);
        CHECK (file.existsAsFile(), "fichier du preset absent");
        CHECK (p.presets.getCurrent().user && p.presets.getDisplayName() == "Darkpsy Crack", "preset en cours incorrect après enregistrement");
        CHECK (! p.presets.isModified(), "marqué modifié juste après l'enregistrement");

        // Modifier -> étoile ; recharger -> valeurs du fichier, plus d'étoile
        setParam (p, P_A_ID, P_A_OTHER);
        setParam (p, P_LEVEL_ID, 3.0f);
        CHECK (p.presets.isModified(), "modification non détectée");
        CHECK (p.presets.load (file), "rechargement impossible");
        CHECK (std::abs (p.apvts.getRawParameterValue (P_A_ID)->load() - aBefore) < 1e-3f, "valeur non restaurée");
        CHECK (std::abs (p.apvts.getRawParameterValue (P_LEVEL_ID)->load() + 7.5f) < 1e-3f, "LEVEL non restauré");
        CHECK (! p.presets.isModified(), "marqué modifié après chargement");

        // Tous les paramètres : enregistrer puis recharger donne exactement les mêmes valeurs
        {
            juce::Random r (7);
            for (auto* prm : p.getParameters())
                prm->setValueNotifyingHost (r.nextFloat());
            std::vector<float> saved;
            for (auto* prm : p.getParameters())
                saved.push_back (prm->getValue());
            CHECK (p.presets.save ("Random"), "enregistrement Random impossible");
            for (auto* prm : p.getParameters())
                prm->setValueNotifyingHost (prm->getDefaultValue());
            CHECK (p.presets.load (dir.getChildFile ("Random" + ext)), "chargement Random impossible");
            float worst = 0.0f;
            for (int i = 0; i < p.getParameters().size(); ++i)
                worst = juce::jmax (worst, std::abs (p.getParameters()[i]->getValue() - saved[(size_t) i]));
            CHECK (worst < 1e-4f, "aller-retour imparfait (écart %g)", (double) worst);
        }

        // Remplacer un preset existant
        setParam (p, P_A_ID, P_A_OTHER);
        CHECK (p.presets.exists ("Random") && p.presets.save ("Random"), "remplacement impossible");
        CHECK (p.presets.getUserPresets().size() == 2, "remplacer a créé un doublon (%d fichiers)", p.presets.getUserPresets().size());

        // Ordre ‹ › : usine puis utilisateur (tri par nom), en boucle
        {
            const auto list = p.presets.getAllEntries();
            CHECK ((int) list.size() == factoryCount + 2, "liste : %d entrées", (int) list.size());
            CHECK (list[(size_t) factoryCount].name == "Darkpsy Crack" && list[(size_t) factoryCount + 1].name == "Random", "tri des presets utilisateur");

            p.presets.loadFactory (0);
            p.presets.step (-1);
            CHECK (p.presets.getDisplayName() == "Random", "‹ depuis le premier ne boucle pas (%s)", p.presets.getDisplayName().toRawUTF8());
            p.presets.step (+1);
            CHECK (! p.presets.getCurrent().user && p.presets.getCurrent().factoryIndex == 0, "› depuis le dernier ne boucle pas");
            for (int i = 0; i < factoryCount; ++i)
                p.presets.step (+1);
            CHECK (p.presets.getDisplayName() == "Darkpsy Crack", "› ne passe pas aux presets utilisateur");
            CHECK (std::abs (p.apvts.getRawParameterValue (P_A_ID)->load() - aBefore) < 1e-3f, "› n'a pas chargé le preset");
            p.presets.loadFactory (1);
            CHECK (p.getMatchingPreset() == 1 && ! p.presets.isModified(), "point de départ via la bibliothèque");
        }

        // L'état du projet garde le preset en cours, et l'étoile
        {
            p.presets.load (file);
            setParam (p, P_A_ID, P_A_OTHER);
            juce::MemoryBlock state;
            p.getStateInformation (state);

            Proc q;
            q.presets.setFolderOverride (dir);
            q.setStateInformation (state.getData(), (int) state.getSize());
            CHECK (q.presets.getCurrent().user && q.presets.getDisplayName() == "Darkpsy Crack", "nom du preset perdu avec le projet");
            CHECK (q.presets.isModified(), "l'étoile est perdue avec le projet");

            p.presets.load (file);
            p.getStateInformation (state);
            Proc r;
            r.setStateInformation (state.getData(), (int) state.getSize());
            CHECK (r.presets.getDisplayName() == "Darkpsy Crack" && ! r.presets.isModified(), "preset non modifié rechargé avec une étoile");

            // Ancien projet (sans nom de preset) : le point de départ est reconnu
            Proc old;
            old.applyPreset (2);
            auto tree = old.apvts.copyState();
            tree.removeProperty ("presetName", nullptr);
            tree.removeProperty ("presetFile", nullptr);
            tree.removeProperty ("presetFactory", nullptr);
            juce::MemoryBlock oldState;
            old.copyXmlToBinary (*tree.createXml(), oldState);
            Proc s;
            s.setStateInformation (oldState.getData(), (int) oldState.getSize());
            CHECK (! s.presets.getCurrent().user && s.presets.getCurrent().factoryIndex == 2, "ancien projet : point de départ non reconnu");
        }

        // Fichiers abîmés : refusés, réglages inchangés
        {
            const auto bad = dir.getChildFile ("Broken" + ext);
            bad.replaceWithText ("<nope");
            const auto other = dir.getChildFile ("Other" + ext);
            other.replaceWithText ("<SomethingElse><PARAM id=\"level\" value=\"-20\"/></SomethingElse>");
            setParam (p, P_LEVEL_ID, 1.0f);
            CHECK (! p.presets.load (bad) && ! p.presets.load (other) && ! p.presets.load (dir.getChildFile ("missing" + ext)), "fichier invalide accepté");
            CHECK (std::abs (p.apvts.getRawParameterValue (P_LEVEL_ID)->load() - 1.0f) < 1e-3f, "un fichier invalide a changé les réglages");

            // Paramètre absent ou valeur folle : défaut / borné, sans plantage
            const auto partial = dir.getChildFile ("Partial" + ext);
            partial.replaceWithText ("<" + juce::String (PLUGIN_NAME) + "Preset version=\"1\"><PARAM id=\"level\" value=\"999\"/><PARAM id=\"" + juce::String (P_A_ID) + "\" value=\"nan\"/></" + juce::String (PLUGIN_NAME) + "Preset>");
            CHECK (p.presets.load (partial), "preset partiel refusé");
            CHECK (std::abs (p.apvts.getRawParameterValue (P_LEVEL_ID)->load() - 6.0f) < 1e-3f, "valeur hors plage non bornée");
            bad.deleteFile();
            other.deleteFile();
            partial.deleteFile();
        }

        // Supprimer : le fichier part, le preset en cours est oublié
        {
            const auto random = dir.getChildFile ("Random" + ext);
            p.presets.load (random);
            CHECK (p.presets.remove (random) && ! random.existsAsFile(), "suppression impossible");
            CHECK (! p.presets.getCurrent().user, "preset supprimé encore en cours");
            CHECK (! p.presets.remove (random), "suppression d'un fichier absent acceptée");
        }

        // Interface : barre des presets
        {
            p.setRateAndBufferSizeDetails (sampleRate, blockSize);
            p.prepareToPlay (sampleRate, blockSize);
            p.presets.loadFactory (0);
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
            auto& facade = dynamic_cast<EDITOR_CLASS*> (editor.get())->getFacade();
            auto* next = dynamic_cast<juce::TextButton*> (facade.findChildWithID ("presetNext"));
            auto* name = dynamic_cast<juce::TextButton*> (facade.findChildWithID ("presetName"));
            auto* save = dynamic_cast<juce::TextButton*> (facade.findChildWithID ("presetSave"));
            CHECK (next != nullptr && name != nullptr && save != nullptr && facade.findChildWithID ("presetPrev") != nullptr, "barre des presets incomplète");

            if (next != nullptr && name != nullptr)
            {
                CHECK (name->getButtonText() == juce::String (Proc::getPresets()[0].name), "nom affiché : %s", name->getButtonText().toRawUTF8());
                next->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                CHECK (name->getButtonText() == juce::String (Proc::getPresets()[1].name), "› dans l'interface : %s", name->getButtonText().toRawUTF8());
                setParam (p, P_A_ID, P_A_OTHER);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                CHECK (name->getButtonText().endsWith (" *"), "étoile absente dans l'interface : %s", name->getButtonText().toRawUTF8());
                CHECK (name->getRight() < 800 && name->getWidth() >= 150, "barre mal placée (%d, %d)", name->getX(), name->getWidth());
            }

            if (save != nullptr)
            {
                save->onClick();
                juce::Component* panel = nullptr;
                for (auto* c : facade.getChildren())
                    if (dynamic_cast<SavePresetPanel*> (c) != nullptr)
                        panel = c;
                CHECK (panel != nullptr && panel->isVisible(), "panneau SAVE non affiché");
                if (panel != nullptr)
                {
                    auto* field = dynamic_cast<juce::TextEditor*> (panel->getChildren()[0]);
                    CHECK (field != nullptr, "champ du nom absent");
                    if (field != nullptr)
                    {
                        field->setText ("From UI", true);
                        field->onReturnKey();
                        CHECK (! panel->isVisible() && dir.getChildFile ("From UI" + ext).existsAsFile(), "SAVE depuis l'interface");
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
                        CHECK (name == nullptr || name->getButtonText() == "From UI", "nom non mis à jour après SAVE");
                    }
                }
            }

            p.editorBeingDeleted (editor.get());
            editor.reset();
        }

        dir.deleteRecursively();
    }

    std::printf ("Interface\n");
    {
        P p;
        prepare (p);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
        CHECK (editor != nullptr, "pas d'interface");
        CHECK (editor->getWidth() == SnareFacade::width && editor->getHeight() == SnareFacade::height, "taille initiale %dx%d", editor->getWidth(), editor->getHeight());

        auto image = editor->createComponentSnapshot (editor->getLocalBounds());
        CHECK (image.isValid(), "rendu de l'interface impossible");

        editor->setSize (600, 432);
        CHECK (editor->getWidth() == 600, "redimensionnement refusé");
        CHECK ((int) p.apvts.state.getProperty ("editorWidth") == 600, "taille non mémorisée");

        auto* ed = dynamic_cast<HyperSnareAudioProcessorEditor*> (editor.get());
        int clicked = 0;
        const auto hitsBefore = p.getHitCount();
        for (auto* child : ed->getFacade().getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
                if (button->getButtonText() == "REAL")
                {
                    button->triggerClick();
                    ++clicked;
                }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        CHECK (clicked == 1, "bouton REAL introuvable");
        CHECK (p.getMatchingPreset() == 3, "le bouton REAL n'a rien changé");

        for (auto* child : ed->getFacade().getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
                if (button->getButtonText() == "SINE")
                    button->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        CHECK (juce::roundToInt (p.apvts.getRawParameterValue (P::bodyId)->load()) == 0, "le bouton SINE n'agit pas");

        for (auto* child : ed->getFacade().getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
                if (button->getButtonText() == "HIT")
                    button->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        run (p, 512, {});
        CHECK (p.getHitCount() == hitsBefore + 1, "HIT ne joue pas");

        p.editorBeingDeleted (editor.get());
        editor.reset();
    }

    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
