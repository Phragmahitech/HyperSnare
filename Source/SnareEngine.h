#pragma once

/*
    HyperSnare — moteur de synthèse de caisse claire (sans JUCE, testable seul).

    Une voix = une frappe. Le moteur est monophonique : chaque frappe coupe la
    précédente en 3 ms.

    Une frappe = deux couches et un claquement :
      - CORPS : SINE (deux sinus façon 808), MEMBRANE (deux modes de peau) ou FM
        (porteuse modulée, plus sale) ; CHARACTER fond vers un banc de 16 modes
        de membrane circulaire. Hauteur TUNE, chute de hauteur PITCH au début.
        Durée BODY.
      - BRUIT (le timbre) : passe-haut puis filtre d'état centré sur COLOR.
        Durée NOISE. Côté acoustique (CHARACTER), le timbre suit le mouvement de
        la peau, arrive un peu après la frappe, et une petite pièce s'ajoute.
      - SNAP : rafale de bruit très courte, passe-haut, pour l'attaque.
    BALANCE dose corps et bruit ; SHAPE fait passer les deux queues de « naturel »
    à « gaté ». Puis, sur la somme : DRIVE (saturation), CRUSH, LEVEL, écrêtage doux.

    Les réglages de timbre sont figés au moment de la frappe ; COLOR, WIDTH,
    DRIVE, CRUSH et LEVEL agissent en continu.
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hypersnare
{

constexpr double pi = 3.14159265358979323846;

enum Body { bodySine = 0, bodyMembrane = 1, bodyFm = 2 };

struct SnareParams
{
    int   body      = bodyFm;
    float balance   = 0.5f;     // 0 = corps, 1 = bruit
    float character = 0.3f;     // 0 = synthétique, 1 = acoustique
    float tuneHz    = 240.0f;   // 100 à 400 Hz
    float pitch     = 14.0f;    // chute de hauteur, demi-tons, 0 à 36
    float colorHz   = 6500.0f;  // centre du timbre, 1 à 12 kHz
    float snap      = 0.8f;     // claquement, 0 à 1
    float bodyMs    = 90.0f;    // 20 à 800 ms
    float noiseMs   = 140.0f;   // 20 à 1500 ms
    float shape     = 0.65f;    // 0 = naturel, 1 = gaté
    float width     = 0.35f;    // 0 = mono, 1 = timbre décorrélé à gauche et à droite
    float variation = 0.2f;     // différences d'une frappe à l'autre
    float drive     = 0.4f;     // saturation, 0 à 1
    float crush     = 0.0f;     // 0 = propre, 1 = 4 bits et échantillonnage divisé par 16
    float levelDb   = 0.0f;
};

//==============================================================================
/** Générateur de bruit (xorshift32), valeurs dans [-1, 1). */
struct Noise
{
    uint32_t state = 0x12345678u;

    void seed (uint32_t s) noexcept        { state = s != 0 ? s : 0x9e3779b9u; }

    uint32_t nextInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float next() noexcept                  { return (float) ((double) nextInt() * (2.0 / 4294967296.0) - 1.0); }
    float unit() noexcept                  { return (float) ((double) nextInt() * (1.0 / 4294967296.0)); }
};

//==============================================================================
/** Filtre d'état (forme TPT de Zavalishin). */
struct Svf
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.4142f;
    float ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;

    void setup (double cutoff, double q, double sampleRate) noexcept
    {
        const double fc = std::min (cutoff, sampleRate * 0.45);
        const double g = std::tan (pi * fc / sampleRate);
        k = (float) (1.0 / q);
        const double d = 1.0 / (1.0 + g * (g + k));
        a1 = (float) d;
        a2 = (float) (g * d);
        a3 = (float) (g * g * d);
    }

    void clear() noexcept                  { ic1 = ic2 = 0; }

    void process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

//==============================================================================
/** Une frappe. */
class SnareVoice
{
public:
    static constexpr int maxModes = 16;

    bool isActive() const noexcept         { return active; }
    bool isFading() const noexcept         { return fading; }

    /** Coupe la frappe en 3 ms. */
    void choke (double rate) noexcept
    {
        if (active && ! fading)
        {
            fading = true;
            fadeGain = 1.0f;
            fadeStep = 1.0f / (float) std::max (1.0, 0.003 * rate);
        }
    }

    void kill() noexcept                   { active = false; fading = false; }

    void start (const SnareParams& p, float velocity, double rate, uint32_t hitSeed) noexcept
    {
        const double sr = rate;
        sampleRate = sr;
        active = true;
        fading = false;
        fadeGain = 1.0f;
        t = 0;

        Noise dice;
        dice.seed (hitSeed * 2654435761u + 17u);
        auto die = [&dice] { return dice.next(); };

        const float c = clamp01 (p.character);
        const float vary = clamp01 (p.variation + 0.25f * c);
        velocity = clamp01 (velocity);

        // Réglages de la frappe, avec ses petites différences
        baseHz = std::clamp ((double) p.tuneHz, 60.0, 600.0) * std::pow (2.0, vary * 0.25 * die() / 12.0);
        pitchSt = std::max (0.0, (double) p.pitch * (1.0 + vary * 0.15 * die()) * (0.75 + 0.25 * velocity));
        colorMul = (float) ((1.0 + vary * 0.15 * die()) * (0.75 + 0.25 * velocity));
        const double bodyMs = std::max (5.0, (double) p.bodyMs * (1.0 + vary * 0.15 * die()));
        const double noiseMs = std::max (5.0, (double) p.noiseMs * (1.0 + vary * 0.18 * die()));
        hitGain = (float) ((0.12 + 0.88 * std::pow (velocity, 1.3f)) * (1.0 + vary * 0.2 * die()));

        body = std::clamp (p.body, 0, 2);
        balance = clamp01 (p.balance);
        modalAmount = c * c * (3.0f - 2.0f * c);
        snap = clamp01 (p.snap) * (0.4f + 0.6f * velocity);
        shape = clamp01 (p.shape);
        couple = 0.7f * c;              // le timbre suit la peau
        const float roomC = std::max (0.0f, (c - 0.5f) * 2.0f);
        roomAmount = 0.22f * roomC * roomC;          // la pièce n'arrive que dans la moitié acoustique

        // Enveloppes : naturelle = exp(-4 t / D), gatée = 1 - (t / D)^6 jusqu'à D
        bodyEnv.setup (bodyMs * 0.001 * sr);
        noiseEnv.setup (noiseMs * 0.001 * sr);
        attack = 0;
        attackStep = 1.0f / (float) std::max (1.0, 0.0001 * sr);

        // Chute de hauteur : 2^(pitch/12 * exp(-t/tau)), tau entre 5 et 18 ms selon la durée du corps
        pitchTau = std::clamp (bodyMs * 0.12, 5.0, 18.0) * 0.001 * sr;
        pitchDecayMul = std::exp (-1.0 / pitchTau);
        pitchNow = 1.0;

        transientMul = (float) std::exp (-1.0 / (0.0006 * sr));
        transientEnv = 1.0f;
        wireDelayMul = (float) std::exp (-1.0 / (0.0025 * sr));   // le timbre arrive ~2 ms après la peau côté acoustique
        wireRise = 0.0f;
        bodyFollow = 0.0f;
        followMul = (float) std::exp (-1.0 / (0.004 * sr));
        fmIndexMul = std::exp (-1.0 / (0.03 * sr));
        fmIndex = 1.0;

        noiseA.seed (hitSeed * 747796405u + 1u);
        noiseB.seed (hitSeed * 2891336453u + 7u);
        noiseC.seed (hitSeed * 1103515245u + 12345u);

        // Corps synthétique
        phase1 = dice.unit() * 0.05;   // départ presque en phase : attaque nette
        phase2 = dice.unit() * 0.05;
        fmModPhase = 0.0;

        // Banc de modes (membrane circulaire, point de frappe à 0,3 du rayon)
        static constexpr double ratios[maxModes] = { 1.000, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501,
                                                     3.600, 3.652, 4.060, 4.154, 4.230, 4.601, 4.832, 4.903 };
        static constexpr double weights[maxModes] = { 1.00, 0.80, 0.62, 0.40, 0.52, 0.34, 0.30, 0.38,
                                                      0.22, 0.26, 0.24, 0.20, 0.18, 0.16, 0.15, 0.12 };
        const double count = 2.0 + 14.0 * c;
        numModes = 0;
        double norm = 0;

        for (int i = 0; i < maxModes; ++i)
        {
            const double w = std::clamp (count - i, 0.0, 1.0) * weights[i];
            if (w <= 0.0)
                continue;

            auto& m = modes[numModes++];
            m.ratio = ratios[i] * (1.0 + vary * 0.004 * die());
            m.phase = dice.unit() * 0.1;
            m.amp = w;
            m.extraMul = std::exp (-(std::pow (ratios[i], 0.8) - 1.0) * 3.0 / std::max (8.0, bodyMs * 0.001 * sr));
            m.extra = 1.0;
            norm += w * w;
        }

        modalNorm = norm > 0 ? (float) (0.9 / std::sqrt (norm)) : 0.0f;

        // Filtres et pièce
        hpL.clear(); hpR.clear(); bpL.clear(); bpR.clear(); snapHp.clear();
        snapHp.setup (2500.0, 0.7071, sr);
        modeTwoEnv = 1.0f;
        modeTwoMul = (float) std::exp (-1.6 / std::max (8.0, bodyMs * 0.001 * sr));   // le 2e mode de la peau s'éteint plus vite
        tailCount = 0;
        lastColor = -1.0f;
        filterCountdown = 0;
        room1.fill (0.0f);
        room2.fill (0.0f);
        roomLen1 = std::min ((int) room1.size() - 1, (int) (0.0071 * sr));
        roomLen2 = std::min ((int) room2.size() - 1, (int) (0.0113 * sr));
        roomPos1 = roomPos2 = 0;
    }

    /** Ajoute la frappe à out[0..n). colorHz et width sont les valeurs courantes (lissées). */
    void render (float* outL, float* outR, int n, float colorHz, float width) noexcept
    {
        if (! active)
            return;

        const float angle = clamp01 (width) * (float) (pi * 0.25);
        const float wc = std::cos (angle), ws = std::sin (angle);
        const bool stereo = ws > 1.0e-4f;

        for (int i = 0; i < n; ++i)
        {
            if (--filterCountdown <= 0)
            {
                updateFilters (colorHz);
                filterCountdown = 32;
            }

            // Hauteur du moment
            const double ratio = std::pow (2.0, pitchSt * pitchNow / 12.0);
            pitchNow *= pitchDecayMul;
            const double hz = baseHz * ratio;

            // Corps
            const float be = bodyEnv.next (shape);
            const float ne = noiseEnv.next (shape);
            attack = std::min (1.0f, attack + attackStep);

            float synth = 0.0f;
            if (modalAmount < 0.999f)
            {
                phase1 += hz / sampleRate;
                if (phase1 >= 1.0) phase1 -= 1.0;

                switch (body)
                {
                    case bodySine:
                    {
                        phase2 += hz * 1.84 / sampleRate;
                        if (phase2 >= 1.0) phase2 -= 1.0;
                        synth = (float) (std::sin (2.0 * pi * phase1) + 0.45 * std::sin (2.0 * pi * phase2)) * (1.0f / 1.2f);
                        break;
                    }
                    case bodyMembrane:
                    {
                        phase2 += hz * 1.594 / sampleRate;
                        if (phase2 >= 1.0) phase2 -= 1.0;
                        synth = (float) (std::sin (2.0 * pi * phase1) + 0.8 * std::sin (2.0 * pi * phase2) * modeTwoEnv) * (1.0f / 1.4f);
                        modeTwoEnv *= modeTwoMul;
                        break;
                    }
                    default:
                    {
                        fmModPhase += hz * 2.31 / sampleRate;
                        if (fmModPhase >= 1.0) fmModPhase -= 1.0;
                        const double index = 0.6 + 4.0 * fmIndex;
                        fmIndex *= fmIndexMul;
                        synth = (float) std::sin (2.0 * pi * phase1 + index * std::sin (2.0 * pi * fmModPhase));
                        break;
                    }
                }
            }

            float modal = 0.0f;
            if (modalAmount > 0.001f)
            {
                double sum = 0;
                for (int k = 0; k < numModes; ++k)
                {
                    auto& m = modes[k];
                    m.phase += hz * m.ratio / sampleRate;
                    m.phase -= std::floor (m.phase);
                    sum += std::sin (2.0 * pi * m.phase) * m.amp * m.extra;
                    m.extra *= m.extraMul;
                }
                modal = (float) sum * modalNorm;
            }

            const float bodySample = (synth * (1.0f - modalAmount) + modal * modalAmount) * be * attack;

            // Le timbre suit la peau (côté acoustique)
            bodyFollow = std::max (std::abs (bodySample), bodyFollow * followMul);
            wireRise = wireDelayMul * wireRise + (1.0f - wireDelayMul);
            const float wireEnv = ne * ((1.0f - couple) + couple * wireRise * std::min (1.0f, 0.4f + 1.6f * bodyFollow));

            const float nA = noiseA.next();
            const float nB = stereo ? noiseB.next() : 0.0f;
            const float noiseLeft = stereo ? wc * nA + ws * nB : nA;
            const float noiseRight = stereo ? wc * nA - ws * nB : nA;

            hpL.process (noiseLeft);
            bpL.process (hpL.hp);
            hpR.process (noiseRight);
            bpR.process (hpR.hp);
            const float wiresL = (1.4f * bpL.bp + 0.2f * bpL.hp) * kNoiseGain * wireEnv * attack;
            const float wiresR = (1.4f * bpR.bp + 0.2f * bpR.hp) * kNoiseGain * wireEnv * attack;

            const float bodyGain = (1.0f - balance) * kBodyGain;
            const float noiseGain = balance;

            float sL = bodyGain * bodySample + noiseGain * wiresL;
            float sR = bodyGain * bodySample + noiseGain * wiresR;

            // Snap
            if (transientEnv > 1.0e-4f)
            {
                snapHp.process (noiseC.next());
                const float click = snapHp.hp * snap * kSnapGain * transientEnv;
                sL += click;
                sR += click;
                transientEnv *= transientMul;
            }

            // Petite pièce (côté acoustique)
            if (roomAmount > 0.001f)
            {
                const float mono = 0.5f * (sL + sR);
                const float d1 = room1[(size_t) roomPos1];
                const float d2 = room2[(size_t) roomPos2];
                room1[(size_t) roomPos1] = mono + 0.45f * d2;
                room2[(size_t) roomPos2] = mono + 0.45f * d1;
                if (++roomPos1 >= roomLen1) roomPos1 = 0;
                if (++roomPos2 >= roomLen2) roomPos2 = 0;
                const float mid = 0.5f * (d1 + d2), side = 0.5f * (d1 - d2) * clamp01 (width);
                sL += roomAmount * (mid + side);
                sR += roomAmount * (mid - side);
            }

            sL *= hitGain;
            sR *= hitGain;

            if (fading)
            {
                sL *= fadeGain;
                sR *= fadeGain;
                fadeGain -= fadeStep;

                if (fadeGain <= 0.0f)
                {
                    active = false;
                    fading = false;
                    return;
                }
            }

            outL[i] += sL;
            outR[i] += sR;
            ++t;

            // Fin : les deux enveloppes sous -90 dB, transitoire fini, pièce éteinte (~150 ms après)
            if (bodyEnv.done() && noiseEnv.done() && transientEnv < 1.0e-4f
                && (roomAmount <= 0.001f || ++tailCount > (int) (0.15 * sampleRate)))
            {
                active = false;
                return;
            }
        }
    }

    // Gains internes (calibrés par Tests/EngineTests.cpp : corps et timbre au même niveau)
    static constexpr float kBodyGain  = 0.55f;
    static constexpr float kNoiseGain = 1.15f;
    static constexpr float kSnapGain  = 1.6f;

private:
    struct Envelope
    {
        double length = 1000, expMul = 1, expNow = 1, gateStep = 0, gatePos = 0;
        float last = 1.0f;

        void setup (double samples) noexcept
        {
            length = std::max (8.0, samples);
            expMul = std::exp (-4.0 / length);
            expNow = 1.0;
            gateStep = 1.0 / length;
            gatePos = 0;
            last = 1.0f;
        }

        float next (float shape) noexcept
        {
            const double g = gatePos;
            double g6 = g * g * g;
            g6 *= g6;
            const double gate = g < 1.0 ? 1.0 - g6 : 0.0;
            last = (float) ((1.0 - shape) * expNow + shape * gate);
            expNow *= expMul;
            gatePos += gateStep;
            return last;
        }

        bool done() const noexcept           { return last < 3.0e-5f && gatePos > 1.0; }
    };

    struct Mode
    {
        double ratio = 1, phase = 0, amp = 0, extra = 1, extraMul = 1;
    };

    static float clamp01 (float x) noexcept  { return std::clamp (x, 0.0f, 1.0f); }

    void updateFilters (float colorHz) noexcept
    {
        const float fc = std::clamp (colorHz * colorMul, 600.0f, 16000.0f);

        if (std::abs (fc - lastColor) < 0.5f)
            return;

        lastColor = fc;
        hpL.setup (fc * 0.3, 0.7071, sampleRate);
        hpR.setup (fc * 0.3, 0.7071, sampleRate);
        bpL.setup (fc, 0.9, sampleRate);
        bpR.setup (fc, 0.9, sampleRate);
    }

    double sampleRate = 48000.0;
    bool active = false, fading = false;
    float fadeGain = 1, fadeStep = 0;
    uint32_t t = 0;
    int tailCount = 0;

    int body = bodyFm;
    float balance = 0.5f, modalAmount = 0, snap = 0, shape = 0, couple = 0, roomAmount = 0;
    float hitGain = 1, colorMul = 1;

    double baseHz = 200, pitchSt = 0, pitchTau = 1, pitchDecayMul = 1, pitchNow = 1;
    Envelope bodyEnv, noiseEnv;
    float attack = 0, attackStep = 1;
    float transientMul = 0, transientEnv = 0, wireDelayMul = 0, wireRise = 0, bodyFollow = 0, followMul = 0;
    double fmIndexMul = 1, fmIndex = 1;
    float modeTwoEnv = 1.0f, modeTwoMul = 0.9998f;

    Noise noiseA, noiseB, noiseC;
    double phase1 = 0, phase2 = 0, fmModPhase = 0;
    Mode modes[maxModes];
    int numModes = 0;
    float modalNorm = 0;

    Svf hpL, hpR, bpL, bpR, snapHp;
    float lastColor = -1.0f;
    int filterCountdown = 0;

    std::array<float, 4096> room1 {}, room2 {};
    int roomLen1 = 100, roomLen2 = 100, roomPos1 = 0, roomPos2 = 0;
};

//==============================================================================
class SnareEngine
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
        snapSmoothers();
    }

    void reset() noexcept
    {
        for (auto& v : voices)
            v.kill();

        holdPhase = 0;
        heldL = heldR = 0;
    }

    void setParams (const SnareParams& p) noexcept     { params = p; }
    const SnareParams& getParams() const noexcept      { return params; }

    /** Fait sonner une frappe tout de suite (velocity de 0 à 1). */
    void trigger (float velocity) noexcept
    {
        for (auto& v : voices)
            v.choke (sampleRate);

        SnareVoice* target = nullptr;
        for (auto& v : voices)
            if (! v.isActive()) { target = &v; break; }

        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.isFading()) { target = &v; break; }

            target->kill();
        }

        ++hitCount;
        target->start (params, std::clamp (velocity, 0.0f, 1.0f), sampleRate, hitCount * 0x9E3779B9u + seedBase);
    }

    void process (float* left, float* right, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            left[i] = right[i] = 0.0f;

        int done = 0;
        while (done < n)
        {
            const int chunk = std::min (32, n - done);
            colorNow += (params.colorHz - colorNow) * blockSmoothing;
            widthNow += (params.width - widthNow) * blockSmoothing;

            for (auto& v : voices)
                v.render (left + done, right + done, chunk, colorNow, widthNow);

            done += chunk;
        }

        const float levelTarget = (float) std::pow (10.0, params.levelDb / 20.0) * kOutputGain;
        const float crushTarget = std::clamp (params.crush, 0.0f, 1.0f);
        const float driveTarget = std::clamp (params.drive, 0.0f, 1.0f);

        for (int i = 0; i < n; ++i)
        {
            levelNow += (levelTarget - levelNow) * sampleSmoothing;
            crushNow += (crushTarget - crushNow) * sampleSmoothing;
            driveNow += (driveTarget - driveNow) * sampleSmoothing;

            float l = left[i], r = right[i];

            // DRIVE : tanh, crête de même hauteur, corps plus dense
            if (driveNow > 0.002f)
            {
                const float g = 1.0f + 11.0f * driveNow * driveNow;
                const float norm = 1.0f / std::tanh (g);
                l = std::tanh (l * g) * norm;
                r = std::tanh (r * g) * norm;
            }

            if (crushNow > 0.002f)
            {
                const double hold = 1.0 + (double) crushNow * crushNow * 15.0 * (sampleRate / 44100.0);
                holdPhase += 1.0;
                if (holdPhase >= hold)
                {
                    holdPhase -= hold;
                    const float steps = std::pow (2.0f, 15.0f - 11.0f * crushNow);
                    heldL = std::round (l * steps) / steps;
                    heldR = std::round (r * steps) / steps;
                }

                const float wet = std::min (1.0f, crushNow * 20.0f);
                if (wet >= 1.0f)
                {
                    l = heldL;
                    r = heldR;
                }
                else
                {
                    l += (heldL - l) * wet;
                    r += (heldR - r) * wet;
                }
            }

            left[i] = softClip (l * levelNow);
            right[i] = softClip (r * levelNow);
        }
    }

    bool isActive() const noexcept
    {
        for (auto& v : voices)
            if (v.isActive())
                return true;

        return false;
    }

    uint32_t getHitCount() const noexcept      { return hitCount; }
    void setSeedBase (uint32_t s) noexcept     { seedBase = s; }

    static float softClip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.8f)
            return x;

        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0 ? -y : y;
    }

    /** Gain de sortie global. */
    static constexpr float kOutputGain = 0.6f;

private:
    void snapSmoothers() noexcept
    {
        colorNow = params.colorHz;
        widthNow = params.width;
        levelNow = (float) std::pow (10.0, params.levelDb / 20.0) * kOutputGain;
        crushNow = params.crush;
        driveNow = params.drive;
        blockSmoothing = (float) (1.0 - std::exp (-32.0 / (0.02 * sampleRate)));
        sampleSmoothing = (float) (1.0 - std::exp (-1.0 / (0.01 * sampleRate)));
    }

    double sampleRate = 48000.0;
    SnareParams params;
    std::array<SnareVoice, 2> voices;
    uint32_t hitCount = 0, seedBase = 0x2545F491u;

    float colorNow = 6500, widthNow = 0.35f, levelNow = 0.6f, crushNow = 0, driveNow = 0;
    float blockSmoothing = 0.1f, sampleSmoothing = 0.01f;
    double holdPhase = 0;
    float heldL = 0, heldR = 0;
};

} // namespace hypersnare
