// Checks the discrete model against independent circuit calculations.
// Exit code is the number of failed checks.
#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

#include "AmpModel.h"

using namespace amp;
using cd = std::complex<double>;

static int failures = 0;

static void check (const char* name, double value, double lo, double hi)
{
    const bool ok = value >= lo && value <= hi && std::isfinite (value);
    std::printf ("%-52s %12.4f   [%g, %g]  %s\n", name, value, lo, hi, ok ? "ok" : "FAIL");
    failures += ok ? 0 : 1;
}

// Independent AC solve of the tone network by complex Gaussian elimination.
static double toneDb (double f, double low, double mid, double high)
{
    using T = ToneNetwork;
    const int n = T::N;
    std::vector<std::vector<cd>> m ((size_t) n, std::vector<cd> ((size_t) n + 1, cd (0.0)));
    const cd s (0.0, 2.0 * pi * f);

    const auto add = [&] (int a, int b, cd y)
    {
        if (a >= 0) m[(size_t) a][(size_t) a] += y;
        if (b >= 0) m[(size_t) b][(size_t) b] += y;
        if (a >= 0 && b >= 0) { m[(size_t) a][(size_t) b] -= y; m[(size_t) b][(size_t) a] -= y; }
    };
    const auto r = [&] (int a, int b, double ohms) { add (a, b, 1.0 / std::max (ohms, 1.0)); };
    const auto c = [&] (int a, int b, double farads) { add (a, b, s * farads); };

    r (T::In, -1, T::R89);
    m[T::In][(size_t) n] = 1.0 / T::R89;
    r (T::In, -1, T::R24);
    r (T::In, T::Rc, T::R94);
    c (T::Rc, -1, T::C59);
    r (T::In, T::A, T::R28);
    c (T::A, T::T, T::C18);
    c (T::A, T::Mw, T::C21);
    c (T::In, T::U, T::CTreble);
    r (T::U, T::W, T::RHigh * (1.0 - high));
    r (T::W, T::T, T::RHigh * high);
    r (T::W, -1, T::RPost);
    r (T::T, T::M, T::RLow * std::pow (low, 2.5));
    r (T::M, -1, T::R95);
    r (T::M, T::Mw, T::RMid * (1.0 - mid));
    r (T::Mw, -1, T::RMid * mid);

    for (int col = 0; col < n; ++col)
    {
        int p = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs (m[(size_t) row][(size_t) col]) > std::abs (m[(size_t) p][(size_t) col])) p = row;
        std::swap (m[(size_t) col], m[(size_t) p]);
        for (int row = 0; row < n; ++row)
        {
            if (row == col) continue;
            const cd k = m[(size_t) row][(size_t) col] / m[(size_t) col][(size_t) col];
            for (int j = col; j <= n; ++j)
                m[(size_t) row][(size_t) j] -= k * m[(size_t) col][(size_t) j];
        }
    }
    const cd out = m[T::W][(size_t) n] / m[T::W][T::W];
    return 20.0 * std::log10 (std::abs (out));
}

static double measureToneDb (ToneNetwork& t, double f, double fs)
{
    t.reset();
    const int settle = (int) (fs * 0.6), n = (int) (std::ceil (f * 0.2) / f * fs);
    double in2 = 0.0, out2 = 0.0;
    for (int i = 0; i < settle + n; ++i)
    {
        const double x = std::sin (2.0 * pi * f * i / fs);
        const double y = t.process (x);
        if (i >= settle) { in2 += x * x; out2 += y * y; }
    }
    return 10.0 * std::log10 (out2 / in2);
}

// Hand-reduced series and parallel impedances, independent of the nodal solver.
static double rhythmDb (double f, double pre, bool crunch)
{
    using R = RhythmNetwork;
    const cd s (0.0, 2.0 * pi * f);
    const auto par = [] (cd a, cd b) { return a * b / (a + b); };
    const double a = R::taper (pre);

    const cd bottom = R::RPot * a;
    const cd bridge = par (R::RPot * (1.0 - a), 1.0 / (s * R::C27));
    const cd branch = bridge + bottom;
    const cd shunt = crunch ? branch : par (branch, R::R23 + 1.0 / (s * R::C63));
    const cd upper = par (R::R21, 1.0 / (s * R::C10) + (crunch ? 1.0 : R::R75));
    const cd atZ = upper + shunt;
    const cd feed = crunch ? par (2.2e6, 1.0 / (s * 470e-12)) : par (470e3, 1.0 / (s * 270e-12));

    const cd h = atZ / (atZ + feed + R::Rs) * shunt / (upper + shunt) * bottom / (bottom + bridge);
    return 20.0 * std::log10 (std::abs (h));
}

// Feedback network reduced by hand to series and parallel impedances.
static double feedbackDb (double f, double resonance, double presence)
{
    using F = FeedbackNetwork;
    const cd s (0.0, 2.0 * pi * f);
    const auto par = [] (cd a, cd b) { return a * b / (a + b); };
    const cd series = F::R56 + par (F::resonanceOhms (resonance), 1.0 / (s * F::C12));
    const cd toQ = 1.0 / (s * F::C30) + F::R57;
    const cd atB = par (1.0 / (s * F::CPresence) + F::presenceOhms (presence), toQ);
    return 20.0 * std::log10 (std::abs (atB / (series + atB) * F::R57 / toQ));
}

static double measureFeedbackDb (FeedbackNetwork& t, double f, double fs)
{
    t.reset();
    const int settle = (int) (fs * 1.0), n = (int) (std::ceil (f * 0.2) / f * fs);
    double in2 = 0.0, out2 = 0.0;
    for (int i = 0; i < settle + n; ++i)
    {
        const double x = std::sin (2.0 * pi * f * i / fs);
        const double y = t.process (x);
        if (i >= settle) { in2 += x * x; out2 += y * y; }
    }
    return 10.0 * std::log10 (out2 / in2);
}

static double measureRhythmDb (RhythmNetwork& t, double f, double fs)
{
    t.reset();
    const int settle = (int) (fs * 0.6), n = (int) (std::ceil (f * 0.2) / f * fs);
    double in2 = 0.0, out2 = 0.0;
    for (int i = 0; i < settle + n; ++i)
    {
        const double x = std::sin (2.0 * pi * f * i / fs);
        const double y = t.process (x);
        if (i >= settle) { in2 += x * x; out2 += y * y; }
    }
    return 10.0 * std::log10 (out2 / in2);
}

int main()
{
    const double fs = 176400.0;

    std::puts ("-- quiescent operating points (V) --");
    Amp a;
    a.prepare (fs);
    const char* names[] = { "V1A", "V1B", "V2A cold", "V2B", "V5B", "V5A feedback", "V3A" };
    const double rails[] = { railV1, railV1, railV1, railV2, railV2, railV2, railV2 };
    for (int i = 0; i < 7; ++i)
    {
        char label[64];
        const auto& s = a.stage (i);
        std::snprintf (label, sizeof label, "%s plate (fraction of rail)", names[i]);
        // V2A is cold-biased by its 39k cathode resistor, so its plate sits close to the rail.
        check (label, s.plate() / rails[i], 0.22, i == 2 ? 0.99 : 0.95);
        std::snprintf (label, sizeof label, "%s cathode", names[i]);
        check (label, s.cathode(), 0.15, 25.0);
    }
    check ("V1A idle current mA (rail drop / 220k)", (railV1 - a.stage (0).plate()) / 220e3 * 1000.0, 0.2, 1.3);
    check ("PI tail current mA", a.inverter().tailCurrent() * 1000.0, 1.0, 12.0);
    check ("PI plate A", a.inverter().plateA(), 130.0, 420.0);
    check ("PI plate B", a.inverter().plateB(), 130.0, 420.0);
    check ("6L6 bias voltage", a.powerStage().biasVoltage(), -80.0, -15.0);

    std::puts ("-- tone network vs independent AC solve (dB error) --");
    {
        ToneNetwork t;
        t.prepare (fs);
        for (double low : { 0.1, 0.5, 1.0 })
            for (double high : { 0.1, 0.5, 0.9 })
            {
                t.setControls (low, 0.5, high);
                for (double f : { 80.0, 400.0, 1500.0, 6000.0 })
                {
                    char label[64];
                    std::snprintf (label, sizeof label, "low %.1f high %.1f %5.0f Hz", low, high, f);
                    check (label, measureToneDb (t, f, fs) - toneDb (f, low, 0.5, high), -0.25, 0.25);
                }
            }
    }

    std::puts ("-- first stage small-signal gain vs tube equations --");
    {
        TriodeStage st;
        st.configure ({ railV1, 220e3, 1820.0, 1.0e-6, 22e3, 0.1e-6, 1.0e6, 68e3, 0.0, 0.0, 0.0 }, fs, 0.0);
        const double f = 1000.0, amp = 2.0e-4;
        const double vp0 = st.plate(), vk0 = st.cathode();
        double sum = 0.0;
        const int settle = (int) (fs * 0.5), n = (int) (fs / f) * 40;
        for (int i = 0; i < settle + n; ++i)
        {
            const double x = amp * std::sin (2.0 * pi * f * i / fs);
            const double y = st.process (x, 0.0) - vp0;
            if (i >= settle) sum += y * std::sin (2.0 * pi * f * i / fs);
        }
        const double measured = -2.0 * sum / n / amp;

        const auto c = triodeCurrent (ax7, 0.0 - vk0, vp0 - vk0);
        const double gain = c.dVgk / (c.dVpk + 1.0 / 220e3);
        // Grid leak divider with 22k source, and a small cathode-cap residual at 1 kHz.
        const double expected = gain * 1.0e6 / (1.0e6 + 22e3);
        check ("V1A gain at 1 kHz", measured, expected * 0.93, expected * 1.05);
    }

    std::puts ("-- behaviour --");
    {
        Controls c;
        c.pre = 0.7; c.post = 0.3; c.cab = false;
        a.setControls (c);

        const auto run = [&] (double amp2, double f) { double peak = 0.0, dc = 0.0; const int n = (int) (fs * 0.5); int cnt = 0;
            for (int i = 0; i < n * 2; ++i) { const double y = a.process (amp2 * std::sin (2.0 * pi * f * i / fs));
                if (i > n) { peak = std::max (peak, std::abs (y)); dc += y; ++cnt; } }
            return std::make_pair (peak, dc / cnt); };

        const auto quiet = run (0.003, 220.0);
        const auto hot = run (0.3, 220.0);
        check ("output peak, 3 mV guitar", quiet.first, 0.02, 1.2);
        check ("output peak, 300 mV guitar", hot.first, 0.05, 1.2);
        check ("compression: output ratio for +40 dB input", hot.first / quiet.first, 0.5, 8.0);
        check ("output DC offset", std::abs (hot.second), 0.0, 0.08);
    }

    std::puts ("-- rhythm network vs hand-reduced impedances (dB error) --");
    {
        RhythmNetwork t;
        t.prepare (fs);
        for (bool crunch : { false, true })
            for (double pre : { 0.2, 0.5, 0.9 })
            {
                t.setControls (pre, crunch);
                for (double f : { 100.0, 800.0, 4000.0 })
                {
                    char label[64];
                    std::snprintf (label, sizeof label, "crunch %d pre %.1f %5.0f Hz", crunch ? 1 : 0, pre, f);
                    check (label, measureRhythmDb (t, f, fs) - rhythmDb (f, pre, crunch), -0.25, 0.25);
                }
            }
    }

    std::puts ("-- feedback network vs hand-reduced impedances (dB error) --");
    {
        FeedbackNetwork t;
        t.prepare (fs);
        for (double res : { 0.0, 0.5, 1.0 })
            for (double pres : { 0.0, 0.5, 1.0 })
            {
                t.setControls (res, pres);
                for (double f : { 60.0, 400.0, 4000.0 })
                {
                    char label[64];
                    std::snprintf (label, sizeof label, "res %.1f pres %.1f %5.0f Hz", res, pres, f);
                    check (label, measureFeedbackDb (t, f, fs) - feedbackDb (f, res, pres), -0.25, 0.25);
                }
            }

        // More feedback removed means more gain from the amp at that frequency.
        check ("Presence up removes high feedback (dB drop at 4 kHz)", feedbackDb (4000.0, 0.5, 0.0) - feedbackDb (4000.0, 0.5, 1.0), 6.0, 60.0);
        check ("Resonance up removes low feedback (dB drop at 80 Hz)", feedbackDb (80.0, 0.0, 0.5) - feedbackDb (80.0, 1.0, 0.5), 3.0, 60.0);
        check ("Resonance barely touches highs (dB change at 6 kHz)", std::abs (feedbackDb (6000.0, 0.0, 0.5) - feedbackDb (6000.0, 1.0, 0.5)), 0.0, 3.0);
    }

    std::puts ("-- channels --");
    {
        Amp q;
        q.prepare (fs);
        Controls c;
        c.cab = false;
        c.post = c.rhythmPost = 0.3;

        const auto level = [&] (bool lead, bool crunch)
        {
            c.lead = lead;
            c.crunch = crunch;
            q.setControls (c);
            double rms = 0.0;
            const int n = (int) (fs * 0.5);
            for (int i = 0; i < n * 2; ++i)
            {
                const double y = q.process (0.01 * std::sin (2.0 * pi * 330.0 * i / fs));
                if (i > n) rms += y * y;
            }
            return std::sqrt (rms / n);
        };

        const double lead = level (true, false);
        const double clean = level (false, false);
        const double crunchy = level (false, true);
        std::printf ("rms: lead %.4f, rhythm %.4f, crunch %.4f\n", lead, clean, crunchy);
        check ("rhythm below lead (dB)", 20.0 * std::log10 (lead / clean), 3.0, 60.0);
        check ("crunch above rhythm clean (dB)", 20.0 * std::log10 (crunchy / clean), 1.0, 50.0);

        // Repeated channel changes while playing must not spike or destabilise.
        double worst = 0.0;
        for (int i = 0; i < (int) fs * 3; ++i)
        {
            if (i % 8000 == 0)
            {
                c.lead = ! c.lead;
                c.crunch = (i / 8000) % 3 == 0;
                q.setControls (c);
            }
            const double y = q.process (0.1 * std::sin (2.0 * pi * 196.0 * i / fs));
            worst = std::isfinite (y) ? std::max (worst, std::abs (y)) : 1.0e12;
        }
        check ("peak during repeated channel switching", worst, 0.0, 1.5);
    }

    std::puts ("-- crackle regression: chord through the user's reported settings --");
    {
        Amp q;
        q.prepare (fs);
        Controls c;
        c.pre = 0.41; c.low = 0.62; c.mid = 0.74; c.high = 0.81; c.post = 0.17;
        c.resonance = 0.53; c.presence = 0.6; c.cab = false;
        q.setControls (c);

        double worstGrid = 0.0, worstStep = 0.0, previous = q.stage (5).plate(), lastStep = 0.0;
        int reversals = 0;
        for (int i = 0; i < (int) (fs * 6.0); ++i)
        {
            const double t = i / fs, ph = std::fmod (t, 0.8);
            const double env = std::exp (-ph * 2.5) * std::min (1.0, ph / 0.003) * std::min (1.0, (0.8 - ph) / 0.003);
            q.process (0.15 * env * (std::sin (2.0 * pi * 82.4 * t) + 0.7 * std::sin (2.0 * pi * 123.5 * t)
                                     + 0.5 * std::sin (2.0 * pi * 164.8 * t) + 0.2 * std::sin (2.0 * pi * 659.0 * t)));
            const double plate = q.stage (5).plate();
            worstGrid = std::max (worstGrid, q.stage (5).grid() - q.stage (5).cathode());
            const double step = plate - previous;
            worstStep = std::max (worstStep, std::abs (step));
            if (step * lastStep < 0.0 && std::min (std::abs (step), std::abs (lastStep)) > 5.0)
                ++reversals;
            lastStep = step;
            previous = plate;
        }
        // Grid conduction holds a positive grid to well under a volt or two above the cathode.
        check ("V5A worst grid above cathode (V)", worstGrid, 0.0, 3.0);
        // A plate can legitimately slew this fast. Numerical faults show up as large
        // sample-to-sample reversals, which is what the fixed solver must not produce.
        check ("V5A worst plate step per sample (V)", worstStep, 0.0, 80.0);
        check ("V5A large sample-to-sample reversals", reversals, 0.0, 0.0);
    }

    std::puts ("-- overload recovery: a very hot input must not latch the amp --");
    {
        Amp q;
        q.prepare (fs);
        Controls c;
        c.cab = false;
        q.setControls (c);

        const auto play = [&] (double level, double seconds, double* worstGrid)
        {
            double peak = 0.0;
            const int n = (int) (seconds * fs);
            for (int i = 0; i < n; ++i)
            {
                const double t = i / fs;
                const double y = q.process (level * (std::sin (2.0 * pi * 110.0 * t) + 0.5 * std::sin (2.0 * pi * 165.0 * t)));
                if (i > n / 2) peak = std::max (peak, std::isfinite (y) ? std::abs (y) : 1.0e9);
                if (worstGrid != nullptr)
                    for (int s = 0; s < 7; ++s)
                        *worstGrid = std::max (*worstGrid, q.stage (s).grid() - q.stage (s).cathode());
            }
            return peak;
        };

        const double before = play (0.15, 0.6, nullptr);
        double worstGrid = 0.0;
        for (double level : { 10.0, 30.0, 100.0, 1000.0 })
        {
            char label[64];
            std::snprintf (label, sizeof label, "output is alive at input level %.0f", level);
            check (label, play (level, 0.4, &worstGrid), 1.0e-3, 4.0);
        }
        // At the absurd 1000-level input, about 1430 V peak through 68k + 22k against the 2k grid path is roughly 31 V.
        check ("worst grid above cathode during overload (V)", worstGrid, 0.0, 35.0);
        const double after = play (0.15, 1.0, nullptr);
        check ("level after overload vs before (ratio)", after / before, 0.7, 1.3);
    }

    std::puts ("-- hot boosted signal must stay stable --");
    {
        // A clean mid-boosted signal, like an overdrive pedal used as a boost, hitting the amp hard.
        const int period = (int) std::round (fs / 87.5);
        const double f = fs / period;
        double worst = -200.0;
        for (double level : { 0.175, 0.2, 0.3, 0.4, 0.45, 0.7, 0.75, 1.0, 1.25 })
        {
            Amp q;
            q.prepare (fs);
            Controls c;
            c.pre = 0.72; c.low = 0.62; c.mid = 0.74; c.high = 0.65; c.post = 0.3; c.resonance = 0.5; c.presence = 0.65; c.cab = false;
            q.setControls (c);

            const double lpCoef = 1.0 - std::exp (-2.0 * pi * 720.0 / fs);
            double low = 0.0;
            const int settle = (int) (fs * 5.0), N = period * 20; // long enough for the supply rails to settle
            std::vector<double> y ((size_t) N);
            for (int i = 0; i < settle + N; ++i)
            {
                const double t = i / fs;
                double x = 0.0;
                for (int h = 1; h <= 10; ++h)
                    x += level / h * std::sin (2.0 * pi * f * 2.0 * h * t + 0.3 * h);
                low += (x - low) * lpCoef;
                const double v = q.process (x + 6.0 * (x - low));
                if (i >= settle)
                    y[(size_t) (i - settle)] = v;
            }
            double d = 0.0, e = 0.0;
            for (int i = period; i < N; ++i)
            {
                const double r = y[(size_t) i] - y[(size_t) (i - period)];
                d += r * r;
                e += y[(size_t) i] * y[(size_t) i];
            }
            worst = std::max (worst, 10.0 * std::log10 ((d + 1e-30) / (e + 1e-30)));
        }
        check ("worst non-repeating part of a steady note (dB)", worst, -200.0, -70.0);
    }
    std::puts ("-- power stage against measurements of a real amp --");
    {
        // Sustained 1 kHz drive with the power supply sagging, as in a real full-power test.
        const double f = fs / 176.0, z = PowerStage::Zaa;
        PowerStage s;
        s.prepare (fs);
        s.setSourceDc (0.0, 0.0);
        PowerSupply sup;
        sup.prepare (fs);
        s.process (0.0, 0.0);
        const double idle = s.plateCurrent();
        const int settle = (int) (fs * 0.5), n = 176 * 40;
        double sum2 = 0.0;
        for (int i = 0; i < settle + n; ++i)
        {
            const double v = 80.0 * std::sin (2.0 * pi * f * i / fs);
            s.setRails (sup.plate(), sup.screen());
            const double y = s.process (v, -v);
            const double d = s.plateCurrent() - idle;
            sup.update (d, d * PowerSupply::screenShare, 0.0, 0.0, 0.0);
            if (i >= settle)
                sum2 += y * y;
        }
        const double power = sum2 / n / z;
        check ("full power with sag (W, measured 120)", power, 110.0, 130.0);
        check ("voltage into 8 ohm (V rms, measured 31)", std::sqrt (power * 8.0), 29.5, 32.5);
        check ("plate sag at full power (V)", railPlate - sup.plate(), 8.0, 45.0);

        PowerStage fresh;
        fresh.prepare (fs);
        check ("bias voltage (measured -58)", fresh.biasVoltage(), -58.5, -57.5);
        double ip, slope;
        Pentode::plate (fresh.biasVoltage(), railPlate, railScreen, ip, slope);
        check ("idle current per tube (mA, stock amps 11-18)", ip * 1000.0, 10.0, 18.0);
    }

    std::puts ("-- power supply sag in the whole amp --");
    {
        Amp q;
        q.prepare (fs);
        Controls c;
        c.pre = 0.72; c.post = 0.7; c.cab = false;
        q.setControls (c);

        check ("no sag at idle (plate rail change, V)", std::abs (q.powerSupply().plate() - railPlate), 0.0, 0.5);

        const auto play = [&] (double level, double seconds)
        {
            for (int i = 0; i < (int) (seconds * fs); ++i)
                q.process (level * std::sin (2.0 * pi * 110.0 * i / fs));
        };

        play (0.3, 1.5);
        const double loaded = q.powerSupply().plate();
        const double preampLoaded = q.powerSupply().rail2();
        check ("plate rail drops when played hard (V)", railPlate - loaded, 3.0, 60.0);
        check ("preamp rail stays in a sensible range (V)", preampLoaded, railV2 - 60.0, railV2 + 15.0);

        play (0.0, 0.5);
        check ("plate rail recovers in silence (V from idle)", std::abs (q.powerSupply().plate() - railPlate), 0.0, 2.0);
    }

    std::puts ("-- preamp rails against the supply chain --");
    {
        Amp q;
        q.prepare (fs);
        const auto v = [&] (int s) { return q.stage (s).plate(); };
        const double i1 = (railV1 - v (0)) / 220e3 + (railV1 - v (1)) / 100e3 + (railV1 - v (2)) / 100e3;
        const double i2 = (railV2 - v (3)) / 220e3 + (railV2 - v (4)) / 220e3 + (railV2 - v (5)) / 100e3 + (railV2 - v (6)) / 68e3;
        const double i3 = q.inverter().tailCurrent();
        const double n3 = 468.0 - 4e3 * (i1 + i2 + i3), n2 = n3 - 22e3 * (i1 + i2), n1 = n2 - 10e3 * i1;
        check ("V3 rail minus supply-chain value (V)", railV3 - n3, -3.0, 3.0);
        check ("V2 rail minus supply-chain value (V)", railV2 - n2, -3.0, 3.0);
        check ("V1 rail minus supply-chain value (V)", railV1 - n1, -3.0, 3.0);
    }
    std::puts ("-- stability --");
    for (double rate : { 88200.0, 176400.0, 384000.0 })
    {
        Amp q;
        q.prepare (rate);
        Controls c;
        c.cab = false;
        double worst = 0.0;
        unsigned seed = 1;
        for (int i = 0; i < (int) rate * 2; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            if (i % 200 == 0)
            {
                const double t = std::fmod (i * 0.000173, 1.0);
                c.pre = t; c.low = 1.0 - t; c.high = t; c.post = 0.5 + 0.5 * t; c.presence = t; c.resonance = 1.0 - t;
                q.setControls (c);
            }
            const double y = q.process (((seed >> 8) / 8388608.0 - 1.0));
            worst = std::isfinite (y) ? std::max (worst, std::abs (y)) : 1.0e12;
        }
        char label[64];
        std::snprintf (label, sizeof label, "noise plus control sweep at %.0f Hz", rate);
        check (label, worst, 0.0, 6.0);
    }

    std::puts ("-- cost --");
    {
        Amp q;
        q.prepare (fs);
        Controls c; c.pre = 0.8;
        q.setControls (c);
        const int n = (int) fs * 5;
        volatile double sink = 0.0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i)
            sink = q.process (0.3 * std::sin (2.0 * pi * 220.0 * i / fs));
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
        check ("CPU at 44.1 kHz x4 (% of one core)", 100.0 * took.count() / 5.0, 0.0, 60.0);
    }

    std::printf ("\n%d check(s) failed\n", failures);
    return failures;
}
