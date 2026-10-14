// Tests du moteur HyperSnare (sans JUCE).

#include "SnareEngine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace hypersnare;

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (%s:%d) ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Render
{
    std::vector<float> left, right;
};

struct Hit { double atMs; float velocity; };

static Render render (const SnareParams& p, const std::vector<Hit>& hits, double ms, double sr = 48000.0, SnareEngine* engineOut = nullptr)
{
    SnareEngine local;
    SnareEngine& e = engineOut != nullptr ? *engineOut : local;
    e.setParams (p);
    e.prepare (sr);

    const int total = (int) (ms * 0.001 * sr);
    Render r;
    r.left.assign ((size_t) total, 0.0f);
    r.right.assign ((size_t) total, 0.0f);

    int pos = 0;
    size_t next = 0;

    while (pos < total)
    {
        int end = total;
        if (next < hits.size())
            end = std::min (total, (int) (hits[next].atMs * 0.001 * sr));

        if (end > pos)
        {
            e.process (r.left.data() + pos, r.right.data() + pos, end - pos);
            pos = end;
        }

        if (next < hits.size() && pos >= (int) (hits[next].atMs * 0.001 * sr))
        {
            e.trigger (hits[next].velocity);
            ++next;
        }
    }

    return r;
}

static float peakOf (const std::vector<float>& x)
{
    float pk = 0;
    for (float v : x)
        pk = std::max (pk, std::abs (v));
    return pk;
}

static double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min (to, x.size());
    if (to <= from) return 0.0;
    double s = 0;
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (to - from));
}

static bool allFinite (const Render& r)
{
    for (size_t i = 0; i < r.left.size(); ++i)
        if (! std::isfinite (r.left[i]) || ! std::isfinite (r.right[i]))
            return false;
    return true;
}

/** Temps (ms) après lequel l'enveloppe (RMS sur 2 ms) reste sous `db` sous la crête. */
static double timeBelow (const std::vector<float>& x, double db, double sr)
{
    const size_t win = (size_t) (0.002 * sr);
    double peakRms = 0;
    std::vector<double> env;
    for (size_t i = 0; i + win <= x.size(); i += win)
    {
        env.push_back (rmsOf (x, i, i + win));
        peakRms = std::max (peakRms, env.back());
    }

    const double threshold = peakRms * std::pow (10.0, db / 20.0);
    size_t last = 0;
    for (size_t i = 0; i < env.size(); ++i)
        if (env[i] > threshold)
            last = i;

    return (double) ((last + 1) * win) * 1000.0 / sr;
}

static double zeroCrossRate (const std::vector<float>& x, size_t from, size_t to)
{
    int n = 0;
    to = std::min (to, x.size());
    for (size_t i = from + 1; i < to; ++i)
        if ((x[i - 1] < 0) != (x[i] < 0)) ++n;
    return (double) n / (double) (to - from);
}

static SnareParams plain()
{
    SnareParams p;
    p.variation = 0.0f;
    p.width = 0.0f;
    p.crush = 0.0f;
    p.drive = 0.0f;
    return p;
}

int main()
{
    const double sr = 48000.0;

    std::printf ("Silence avant toute frappe\n");
    {
        auto r = render (plain(), {}, 200);
        CHECK (peakOf (r.left) == 0.0f && peakOf (r.right) == 0.0f, "sortie non nulle sans frappe");
    }

    std::printf ("Tous les corps, balances et characters : audible, fini, borné, voix qui s'éteint\n");
    for (int body = 0; body < 3; ++body)
        for (float c : { 0.0f, 0.5f, 1.0f })
            for (float b : { 0.0f, 0.5f, 1.0f })
                for (float drive : { 0.0f, 1.0f })
                {
                    SnareParams p = plain();
                    p.body = body; p.character = c; p.balance = b; p.drive = drive;
                    SnareEngine e;
                    auto r = render (p, { { 0.0, 1.0f } }, 1500.0, sr, &e);
                    const float pk = peakOf (r.left);
                    CHECK (allFinite (r), "NaN/inf corps %d c=%.1f b=%.1f", body, c, b);
                    CHECK (pk <= 1.0f, "dépasse 0 dBFS (%.3f)", pk);
                    CHECK (pk > 0.15f, "trop faible : corps %d c=%.1f b=%.1f drive=%.0f crête %.3f", body, c, b, drive, pk);
                    CHECK (! e.isActive(), "voix encore active à la fin (corps %d c=%.1f)", body, c);
                }

    std::printf ("Équilibre corps / bruit : moins de 7 dB d'écart\n");
    for (int body = 0; body < 3; ++body)
        for (float c : { 0.0f, 1.0f })
        {
            SnareParams p = plain();
            p.body = body; p.character = c; p.snap = 0; p.shape = 1; p.bodyMs = 200; p.noiseMs = 200; p.pitch = 0;
            p.balance = 0; auto bodyOnly = render (p, { { 0, 1 } }, 200);
            p.balance = 1; auto noiseOnly = render (p, { { 0, 1 } }, 200);
            const double ratio = 20.0 * std::log10 (rmsOf (bodyOnly.left, 0, 9600) / rmsOf (noiseOnly.left, 0, 9600));
            CHECK (std::abs (ratio) < 7.0, "corps %d c=%.0f : corps à %.1f dB du bruit", body, c, ratio);
        }

    std::printf ("Durées : BODY et NOISE indépendants, proches du réglage\n");
    for (float D : { 40.0f, 150.0f, 500.0f })
    {
        SnareParams p = plain();
        p.shape = 0; p.snap = 0; p.character = 0;
        p.balance = 0; p.bodyMs = D; p.noiseMs = 20; p.pitch = 0;
        auto b = render (p, { { 0, 1 } }, D * 3.0 + 50);
        const double tb = timeBelow (b.left, -35.0, sr);
        CHECK (tb > D * 0.7 && tb < D * 1.4, "BODY %.0f ms : -35 dB à %.1f ms", D, tb);

        p.balance = 1; p.noiseMs = D; p.bodyMs = 20;
        auto n = render (p, { { 0, 1 } }, D * 3.0 + 50);
        const double tn = timeBelow (n.left, -35.0, sr);
        CHECK (tn > D * 0.7 && tn < D * 1.4, "NOISE %.0f ms : -35 dB à %.1f ms", D, tn);
    }

    std::printf ("SHAPE gaté : plus rien après la plus longue durée\n");
    {
        SnareParams p = plain();
        p.shape = 1; p.bodyMs = 120; p.noiseMs = 200; p.snap = 0;
        auto r = render (p, { { 0, 1 } }, 400);
        CHECK (rmsOf (r.left, (size_t) (0.205 * sr), (size_t) (0.4 * sr)) < 1e-6, "son après la fin du gate");
        CHECK (rmsOf (r.left, (size_t) (0.15 * sr), (size_t) (0.17 * sr)) > 0.01, "le timbre ne tient pas jusqu'à NOISE");
    }

    std::printf ("Une frappe coupe la précédente\n");
    {
        SnareParams p = plain();
        p.bodyMs = 800; p.noiseMs = 1500; p.shape = 0;
        auto alone = render (p, { { 100, 1 } }, 600);          // la 2e frappe seule
        auto two = render (p, { { 0, 1 }, { 100, 1 } }, 600);
        const double a = rmsOf (alone.left, (size_t) (0.4 * sr), (size_t) (0.6 * sr));
        const double b = rmsOf (two.left, (size_t) (0.4 * sr), (size_t) (0.6 * sr));
        CHECK (a > 0.001, "la frappe seule devrait encore sonner");
        CHECK (std::abs (b - a) < a * 0.05, "la 1re frappe déborde sur la 2e (%.4f contre %.4f)", b, a);
    }

    std::printf ("TUNE et PITCH\n");
    for (int body = 0; body < 3; ++body)
    {
        SnareParams p = plain();
        p.balance = 0; p.body = body; p.pitch = 0; p.snap = 0; p.bodyMs = 400; p.shape = 1;
        p.tuneHz = 120; auto low = render (p, { { 0, 1 } }, 200);
        p.tuneHz = 380; auto high = render (p, { { 0, 1 } }, 200);
        const double zl = zeroCrossRate (low.left, 2400, 9600), zh = zeroCrossRate (high.left, 2400, 9600);
        CHECK (zh > zl * 2.0, "TUNE sans effet (corps %d : %.4f / %.4f)", body, zl, zh);
    }
    {
        SnareParams p = plain();
        p.balance = 0; p.body = bodySine; p.tuneHz = 200; p.snap = 0; p.bodyMs = 400; p.shape = 1;
        p.pitch = 24;
        auto r = render (p, { { 0, 1 } }, 300);
        const double early = zeroCrossRate (r.left, 0, 240);              // 5 premières ms
        const double late = zeroCrossRate (r.left, 7200, 12000);          // 150 à 250 ms
        CHECK (early > late * 2.0, "pas de chute de hauteur (%.4f / %.4f)", early, late);
        const double expected = 2.0 * 200.0 / sr;                          // un sinus : 2 passages par période
        CHECK (std::abs (late - expected) < expected * 0.35, "hauteur finale fausse (%.4f, attendu ~%.4f)", late, expected);
    }

    std::printf ("COLOR, vélocité, LEVEL, SNAP, DRIVE\n");
    {
        SnareParams p = plain();
        p.balance = 1; p.colorHz = 1500;
        auto low = render (p, { { 0, 1 } }, 200);
        p.colorHz = 11000;
        auto high = render (p, { { 0, 1 } }, 200);
        CHECK (zeroCrossRate (high.left, 0, 4800) > zeroCrossRate (low.left, 0, 4800) * 1.5, "COLOR n'éclaircit pas");

        SnareParams v = plain();
        auto loud = render (v, { { 0, 1.0f } }, 300);
        auto soft = render (v, { { 0, 0.2f } }, 300);
        CHECK (rmsOf (soft.left, 0, 14400) < rmsOf (loud.left, 0, 14400) * 0.4, "vélocité faible pas assez douce");

        v.levelDb = -12;
        auto quiet = render (v, { { 0, 1.0f } }, 300);
        const double r = rmsOf (quiet.left, 0, 14400) / rmsOf (loud.left, 0, 14400);
        CHECK (r > 0.2 && r < 0.3, "LEVEL -12 dB donne un rapport %.3f", r);

        SnareParams s = plain();
        s.snap = 0; auto noSnap = render (s, { { 0, 1 } }, 20);
        s.snap = 1; auto withSnap = render (s, { { 0, 1 } }, 20);
        CHECK (rmsOf (withSnap.left, 0, 48) > rmsOf (noSnap.left, 0, 48) * 1.3, "SNAP sans effet sur l'attaque");

        SnareParams d = plain();
        d.drive = 0; auto clean = render (d, { { 0, 1 } }, 300);
        d.drive = 1; auto driven = render (d, { { 0, 1 } }, 300);
        CHECK (rmsOf (driven.left, 0, 14400) > rmsOf (clean.left, 0, 14400) * 1.4, "DRIVE ne densifie pas");
        CHECK (peakOf (driven.left) <= 1.0f, "DRIVE dépasse 0 dBFS");
    }

    std::printf ("Largeur : mono strict à 0, timbre décorrélé à 100 %%\n");
    {
        SnareParams p = plain();
        p.variation = 0.6f; p.width = 0.0f;
        auto mono = render (p, { { 0, 1 }, { 300, 0.7f } }, 700);
        bool same = true;
        for (size_t i = 0; i < mono.left.size(); ++i) if (mono.left[i] != mono.right[i]) { same = false; break; }
        CHECK (same, "gauche et droite différents avec WIDTH à 0");

        p.width = 1.0f; p.balance = 1.0f; p.snap = 0;
        auto wide = render (p, { { 0, 1 } }, 300);
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < wide.left.size(); ++i) { lr += wide.left[i] * wide.right[i]; ll += wide.left[i] * wide.left[i]; rr += wide.right[i] * wide.right[i]; }
        CHECK (std::abs (lr / std::sqrt (ll * rr)) < 0.3, "timbre pas décorrélé (%.2f)", lr / std::sqrt (ll * rr));
    }

    std::printf ("CRUSH\n");
    {
        SnareParams p = plain();
        p.crush = 1.0f;
        auto r = render (p, { { 0, 1 } }, 300);
        int repeats = 0;
        for (size_t i = 1; i < 4800; ++i) if (r.left[i] == r.left[i - 1]) ++repeats;
        CHECK (repeats > 4000, "CRUSH ne réduit pas l'échantillonnage (%d)", repeats);
    }

    std::printf ("Reproductible, VARIATION efficace\n");
    {
        SnareParams p;
        std::vector<Hit> hits;
        for (int i = 0; i < 16; ++i) hits.push_back ({ i * 78.9, 0.6f + 0.025f * i });
        auto a = render (p, hits, 1500);
        auto b = render (p, hits, 1500);
        CHECK (a.left == b.left && a.right == b.right, "deux rendus identiques diffèrent");

        SnareParams v = plain();
        v.variation = 1.0f;
        auto twice = render (v, { { 0, 1 }, { 100, 1 } }, 200);
        CHECK (std::abs (rmsOf (twice.left, 0, 2400) - rmsOf (twice.left, 4800, 7200)) > 1e-4, "VARIATION sans effet");
    }

    std::printf ("Fréquences d'échantillonnage et réglages au hasard\n");
    {
        Noise rng;
        rng.seed (2024);
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (int round = 0; round < 12; ++round)
            {
                SnareParams p;
                p.body = (int) (rng.unit() * 3) % 3;
                p.balance = rng.unit(); p.character = rng.unit();
                p.tuneHz = 100 * std::pow (4.0f, rng.unit()); p.pitch = 36 * rng.unit();
                p.colorHz = 1000 * std::pow (12.0f, rng.unit()); p.snap = rng.unit();
                p.bodyMs = 20 * std::pow (40.0f, rng.unit()); p.noiseMs = 20 * std::pow (75.0f, rng.unit());
                p.shape = rng.unit(); p.width = rng.unit(); p.variation = rng.unit();
                p.drive = rng.unit(); p.crush = rng.unit();
                p.levelDb = -24 + 30 * rng.unit();

                std::vector<Hit> hits;
                double t = 0;
                while (t < 1500) { hits.push_back ({ t, rng.unit() }); t += 0.5 + 120 * rng.unit() * rng.unit(); }
                auto r = render (p, hits, 1800, rate);
                CHECK (allFinite (r), "NaN à %.0f Hz", rate);
                CHECK (peakOf (r.left) <= 1.0f && peakOf (r.right) <= 1.0f, "dépassement à %.0f Hz", rate);
            }
    }

    std::printf ("Charge processeur (pire cas : acoustique, rolls en triples-croches à 200 BPM)\n");
    {
        SnareParams p = plain();
        p.character = 1; p.width = 1; p.variation = 1; p.drive = 1; p.crush = 0.5f; p.noiseMs = 1500; p.bodyMs = 800; p.shape = 0;
        std::vector<Hit> hits;
        for (int i = 0; i < 800; ++i) hits.push_back ({ i * 37.5, 1.0f });
        const auto t0 = std::chrono::steady_clock::now();
        auto r = render (p, hits, 30000);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  30 s d'audio en %.3f s (%.2f %% d'un cœur)\n", seconds, seconds / 30.0 * 100.0);
        CHECK (seconds < 3.0, "trop lent");
        CHECK (allFinite (r), "NaN");
    }

    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
