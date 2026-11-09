// Circuit model of a 5150-style high-gain lead channel. No JUCE dependency.
//
// Component values come from the 1992 service schematic (sheet numbers 81501510).
// Tube data uses the Koren model. Values marked ESTIMATE are not on the schematic.
//
//   V1A > pre network > V1B > V2A (cold) > V2B > V5B > V5A (shunt feedback)
//       > tone stack > post > V3A > long-tail phase inverter > 4x 6L6GC > speaker load
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace amp
{
constexpr double pi = 3.14159265358979323846;

// Preamp rails are solved from a measured 468 V supply through the 4k, 22k and 10k dropping resistors.
// Plate voltage was measured on a real amp.
constexpr double railV1 = 328.0, railV2 = 344.0, railV3 = 439.0, railPlate = 478.0, railScreen = 470.0;

struct Triode
{
    double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0, rgi = 2000.0;
};

constexpr Triode ax7 {};

// log(1 + e^x) and its slope. Beyond the range where e^x is safe it continues as
// the straight line it approaches, so the value and slope always agree.
inline void softPlus (double x, double& value, double& slope)
{
    if (x > 30.0)
    {
        value = x;
        slope = 1.0;
        return;
    }
    const double e = std::exp (std::max (x, -40.0));
    value = std::log1p (e);
    slope = e / (1.0 + e);
}

struct PlateCurrent
{
    double ip, dVgk, dVpk;
};

inline PlateCurrent triodeCurrent (const Triode& t, double vgk, double vpkIn)
{
    const double vpk = std::max (vpkIn, 0.01);
    const double s = std::sqrt (t.kvb + vpk * vpk);
    const double a = t.kp * (1.0 / t.mu + vgk / s);
    double l, dl;
    softPlus (a, l, dl);
    const double e1 = vpk / t.kp * l;

    if (e1 <= 1.0e-9)
        return { 0.0, 0.0, 0.0 };

    const double power = std::pow (e1, t.ex - 1.0) / t.kg1;
    const double scale = t.ex * power;
    return { e1 * power,
             scale * vpk / s * dl,
             scale * (l / t.kp - dl * vpk * vpk * vgk / (s * s * s)) };
}

// Smooth grid conduction, starting near 0 V.
inline void gridCurrent (double vgk, double rgi, double& ig, double& dIg)
{
    constexpr double soft = 0.08;
    double l, dl;
    softPlus (vgk / soft, l, dl);
    ig = soft * l / rgi;
    dIg = dl / rgi;
}

struct OnePole
{
    void setLowPass (double hz, double fs) { const double t = std::tan (pi * std::min (hz, 0.45 * fs) / fs); g = t / (1.0 + t); }
    void reset (double v = 0.0) { s = v; }
    double lowPass (double x)
    {
        const double v = (x - s) * g;
        const double y = v + s;
        s = y + v;
        return y;
    }
    double g = 1.0, s = 0.0;
};

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0), bilinear transform.
    void setAnalog (double n2, double n1, double n0, double d2, double d1, double d0, double fs)
    {
        const double k = 2.0 * fs, k2 = k * k;
        const double norm = 1.0 / (d2 * k2 + d1 * k + d0);
        b0 = (n2 * k2 + n1 * k + n0) * norm;
        b1 = 2.0 * (n0 - n2 * k2) * norm;
        b2 = (n2 * k2 - n1 * k + n0) * norm;
        a1 = 2.0 * (d0 - d2 * k2) * norm;
        a2 = (d2 * k2 - d1 * k + d0) * norm;
    }

    void setPeak (double hz, double q, double db, double fs)
    {
        const double w = 2.0 * pi * hz / fs, a = std::pow (10.0, db / 40.0), al = std::sin (w) / (2.0 * q);
        const double n = 1.0 / (1.0 + al / a);
        b0 = (1.0 + al * a) * n;
        b1 = a1 = -2.0 * std::cos (w) * n;
        b2 = (1.0 - al * a) * n;
        a2 = (1.0 - al / a) * n;
    }

    void setHighPass (double hz, double q, double fs)
    {
        const double w = 2.0 * pi * hz / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), n = 1.0 / (1.0 + al);
        b0 = b2 = (1.0 + c) * 0.5 * n;
        b1 = -(1.0 + c) * n;
        a1 = -2.0 * c * n;
        a2 = (1.0 - al) * n;
    }

    void setLowPass (double hz, double q, double fs)
    {
        const double w = 2.0 * pi * hz / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), n = 1.0 / (1.0 + al);
        b0 = b2 = (1.0 - c) * 0.5 * n;
        b1 = (1.0 - c) * n;
        a1 = -2.0 * c * n;
        a2 = (1.0 - al) * n;
    }
};

struct StageConfig
{
    double vpp, rp, rk, ck;      // rail, plate load, cathode R, cathode bypass (0 = none)
    double rsrc, cc, rl, rs;     // source R, coupling cap, grid leak, grid stopper
    double rf = 0.0, co = 0.0;   // optional plate-to-grid feedback R and output cap
    double millerGain = 0.0;     // voltage gain used for input Miller capacitance (ESTIMATE)
};

// One common-cathode triode: grid, plate and cathode solved together each sample.
class TriodeStage
{
public:
    void configure (const StageConfig& config, double fs, double sourceDc)
    {
        c = config;
        T = 1.0 / fs;
        gc = 2.0 * c.cc * fs;
        gk = c.ck > 0.0 ? 2.0 * c.ck * fs : 0.0;
        gf = c.rf > 0.0 ? 1.0 / c.rf : 0.0;
        gcp = plateCap * fs; // backward Euler: this node is stiff and trapezoidal integration would ring
        // Input capacitance is a real capacitor at the grid, solved with the tube (backward Euler).
        gcg = (1.6e-12 + (1.0 + c.millerGain) * 1.7e-12) * fs;
        reset (sourceDc);
    }

    void setSourceResistance (double rsrc) { c.rsrc = rsrc; }
    void setGridLeak (double rl) { c.rl = rl; }

    // Keeps the cathode voltage continuous when the relay changes the bypass.
    void setCathodeCap (double ck)
    {
        gk = 2.0 * ck / T;
        hk = gk * vk;
    }

    void reset (double sourceDc)
    {
        solveDc();
        hc = gc * sourceDc;
        hk = gk * vk;
        hcp = gcp * vp;
        hcg = gcg * vg;
        vco = vp;
        lastInputCurrent = 0.0;
    }

    double process (double vsrc, double iLoad)
    {
        const double rser = c.rsrc + 1.0 / gc;
        const double voc = (vsrc - hc / gc) * c.rl / (rser + c.rl);
        const double rth = rser * c.rl / (rser + c.rl);
        solve (voc, rth + c.rs, iLoad);

        const double vgk = vg - vk;
        double ig, dIg;
        gridCurrent (vgk, ax7.rgi, ig, dIg);

        const double icg = gcg * vg - hcg;
        hcg = gcg * vg;
        const double vnode = vg + (ig + icg) * c.rs;
        const double i = (vsrc - hc / gc - vnode) / rser;
        const double vcap = (i + hc) / gc;
        hc = gc * vcap + i;
        lastInputCurrent = i;

        if (gk > 0.0)
        {
            const double ik = gk * vk - hk;
            hk = gk * vk + ik;
        }

        hcp = gcp * vp;

        if (c.co > 0.0)
            vco += T / c.co * ((vp - vco - vg) * gf + iLoad);

        return vp;
    }

    double plate() const { return vp; }
    double grid() const { return vg; }
    double cathode() const { return vk; }
    double output() const { return vp - vco; }
    double inputCurrent() const { return lastInputCurrent; }

private:
    void solveDc()
    {
        vg = 0.0;
        vp = c.vpp * 0.55;
        vk = 1.5;
        const double savedGk = gk, savedGf = gf, savedHk = hk, savedGcp = gcp, savedHcp = hcp, savedGcg = gcg, savedHcg = hcg;
        gk = gf = hk = gcp = hcp = gcg = hcg = 0.0;
        vco = 0.0;
        for (int i = 0; i < 60; ++i)
            solve (0.0, c.rl + c.rs, 0.0);
        gk = savedGk;
        gf = savedGf;
        hk = savedHk;
        gcp = savedGcp;
        hcp = savedHcp;
        gcg = savedGcg;
        hcg = savedHcg;
    }

    void solve (double voc, double rg, double iLoad)
    {
        const double gg = 1.0 / rg, gp = 1.0 / c.rp, gkt = 1.0 / c.rk + gk;
        double lastNorm = 0.0, lastDg = 0.0, lastDp = 0.0, lastDk = 0.0;

        for (int iter = 0; iter < 200; ++iter) // rarely used, but a hard sample must always converge
        {
            const auto cur = triodeCurrent (ax7, vg - vk, vp - vk);
            double ig, dIg;
            gridCurrent (vg - vk, ax7.rgi, ig, dIg);

            const double vo = vp - vco;
            const double f1 = (voc - vg) * gg + (vo - vg) * gf - ig - (vg * gcg - hcg);
            const double f2 = (c.vpp - vp) * gp - cur.ip - iLoad - (vo - vg) * gf - (vp * gcp - hcp);
            const double f3 = cur.ip + ig - vk * gkt + hk;

            // If the last step made things worse, go back half way and try again.
            const double norm = std::abs (f1) * rg + std::abs (f2) * c.rp + std::abs (f3) * c.rk;
            if (iter > 0 && norm > lastNorm * 1.5 && std::abs (lastDg) + std::abs (lastDp) + std::abs (lastDk) > 1.0e-3)
            {
                lastDg *= 0.5;
                lastDp *= 0.5;
                lastDk *= 0.5;
                vg -= lastDg;
                vp = std::clamp (vp - lastDp, 0.5, c.vpp + 30.0);
                vk = std::max (vk - lastDk, 0.0);
                continue;
            }
            lastNorm = norm;

            const double a11 = -gg - dIg - gf - gcg, a12 = gf, a13 = dIg;
            const double a21 = -cur.dVgk + gf, a22 = -gp - cur.dVpk - gf - gcp, a23 = cur.dVgk + cur.dVpk;
            const double a31 = cur.dVgk + dIg, a32 = cur.dVpk, a33 = -cur.dVgk - cur.dVpk - dIg - gkt;

            const double c11 = a22 * a33 - a23 * a32, c12 = a23 * a31 - a21 * a33, c13 = a21 * a32 - a22 * a31;
            const double det = a11 * c11 + a12 * c12 + a13 * c13;
            if (std::abs (det) < 1.0e-30)
                break;

            const double r1 = -f1, r2 = -f2, r3 = -f3;
            double dg = (r1 * c11 + r2 * (a13 * a32 - a12 * a33) + r3 * (a12 * a23 - a13 * a22)) / det;
            double dp = (r1 * c12 + r2 * (a11 * a33 - a13 * a31) + r3 * (a13 * a21 - a11 * a23)) / det;
            double dk = (r1 * c13 + r2 * (a12 * a31 - a11 * a32) + r3 * (a11 * a22 - a12 * a21)) / det;

            dg = std::clamp (dg, -40.0, 40.0);
            dp = std::clamp (dp, -60.0, 60.0);
            dk = std::clamp (dk, -10.0, 10.0);

            vg += dg;
            vp = std::clamp (vp + dp, 0.5, c.vpp + 30.0);
            vk = std::max (vk + dk, 0.0);
            lastDg = dg;
            lastDp = dp;
            lastDk = dk;

            if (std::abs (dg) + std::abs (dp) + std::abs (dk) < 1.0e-3)
                break;
        }
    }

    StageConfig c {};
    static constexpr double plateCap = 30e-12; // ESTIMATE: tube and wiring capacitance at the plate

    double T = 0.0, gc = 1.0, gk = 0.0, gf = 0.0, gcp = 0.0, hc = 0.0, hk = 0.0, hcp = 0.0, vco = 0.0;
    double gcg = 0.0, hcg = 0.0, lastInputCurrent = 0.0;
    double vg = 0.0, vp = 200.0, vk = 1.0;
};

template <size_t N>
void invertMatrix (std::array<std::array<double, N>, N> a, std::array<std::array<double, N>, N>& inv)
{
    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < N; ++j)
            inv[i][j] = i == j ? 1.0 : 0.0;

    for (size_t col = 0; col < N; ++col)
    {
        size_t pivot = col;
        for (size_t row = col + 1; row < N; ++row)
            if (std::abs (a[row][col]) > std::abs (a[pivot][col]))
                pivot = row;
        std::swap (a[col], a[pivot]);
        std::swap (inv[col], inv[pivot]);

        const double d = 1.0 / a[col][col];
        for (size_t j = 0; j < N; ++j) { a[col][j] *= d; inv[col][j] *= d; }

        for (size_t row = 0; row < N; ++row)
        {
            if (row == col) continue;
            const double f = a[row][col];
            for (size_t j = 0; j < N; ++j) { a[row][j] -= f * a[col][j]; inv[row][j] -= f * inv[col][j]; }
        }
    }
}

// Rhythm channel input: clean and crunch settings of relay K4 and switch S1.
class RhythmNetwork
{
public:
    static constexpr int N = 6;
    enum Node { S, Z, V, W, Nc, Nm };

    static constexpr double Rs = 60e3, R21 = 1e6, R75 = 2.2e6, R23 = 39e3, RPot = 1e6;
    static constexpr double C10 = 470e-12, C63 = 0.1e-6, C27 = 180e-12;

    void prepare (double sampleRate) { fs = sampleRate; reset(); setControls (0.5, false); }
    void reset() { hist = {}; }

    void setControls (double pre, bool crunch)
    {
        const double a = std::clamp (std::pow (pre, 2.5), 0.002, 0.998);
        std::array<std::array<double, N>, N> g {};
        const auto add = [&] (int p, int q, double y)
        {
            if (p >= 0) g[(size_t) p][(size_t) p] += y;
            if (q >= 0) g[(size_t) q][(size_t) q] += y;
            if (p >= 0 && q >= 0) { g[(size_t) p][(size_t) q] -= y; g[(size_t) q][(size_t) p] -= y; }
        };
        const auto r = [&] (int p, int q, double ohms) { add (p, q, 1.0 / ohms); };

        r (S, -1, Rs);
        r (S, Z, crunch ? 2.2e6 : 470e3);
        r (Z, V, R21);
        r (Nm, V, crunch ? 1.0 : R75);       // S1C shorts R75 when crunch is engaged
        if (! crunch)
            r (V, Nc, R23);                  // K4B removes the shunt when crunch is engaged
        r (V, W, RPot * (1.0 - a));
        r (W, -1, RPot * a);

        const double k = 2.0 * fs;
        capValues = { crunch ? 470e-12 : 270e-12, C10, C63, C27 };
        for (size_t i = 0; i < caps.size(); ++i)
        {
            capG[i] = k * capValues[i];
            add (caps[i].first, caps[i].second, capG[i]);
        }
        invertMatrix<N> (g, inv);
    }

    double process (double vsrc)
    {
        std::array<double, N> rhs {};
        rhs[S] = vsrc / Rs;
        for (size_t i = 0; i < caps.size(); ++i)
        {
            if (caps[i].first >= 0) rhs[(size_t) caps[i].first] += hist[i];
            if (caps[i].second >= 0) rhs[(size_t) caps[i].second] -= hist[i];
        }

        std::array<double, N> v {};
        for (size_t r = 0; r < N; ++r)
            for (size_t c = 0; c < N; ++c)
                v[r] += inv[r][c] * rhs[c];

        for (size_t i = 0; i < caps.size(); ++i)
        {
            const double a = caps[i].first >= 0 ? v[(size_t) caps[i].first] : 0.0;
            const double b = caps[i].second >= 0 ? v[(size_t) caps[i].second] : 0.0;
            hist[i] = 2.0 * capG[i] * (a - b) - hist[i];
        }
        return v[W];
    }

    static double taper (double pre) { return std::clamp (std::pow (pre, 2.5), 0.002, 0.998); }

private:
    static constexpr std::array<std::pair<int, int>, 4> caps { { { S, Z }, { Z, Nm }, { Nc, -1 }, { V, W } } };

    double fs = 192000.0;
    std::array<double, 4> capValues {}, capG {}, hist {};
    std::array<std::array<double, N>, N> inv {};
};

// Tone stack and its input attenuator, solved as a linear nodal network.
class ToneNetwork
{
public:
    static constexpr int N = 8;
    enum Node { In, A, U, T, W, M, Mw, Rc };

    static constexpr double R89 = 470e3, R24 = 33e3, R94 = 47e3, C59 = 0.01e-6;
    static constexpr double R28 = 47e3, C18 = 0.022e-6, C21 = 0.022e-6, CTreble = 570e-12;
    static constexpr double RHigh = 250e3, RLow = 1e6, RMid = 50e3, R95 = 47e3;
    static constexpr double RPost = 500e3; // the rhythm and lead post pots both load this node

    void prepare (double sampleRate) { fs = sampleRate; reset(); setControls (0.5, 0.5, 0.5); }
    void reset() { hist = {}; }

    // low, mid, high in 0..1
    void setControls (double lowIn, double mid, double high)
    {
        const double low = std::pow (lowIn, 2.5); // audio taper, per the schematic's A-suffix pot
        std::array<std::array<double, N>, N> g {};
        const auto add = [&] (int a, int b, double y)
        {
            if (a >= 0) g[(size_t) a][(size_t) a] += y;
            if (b >= 0) g[(size_t) b][(size_t) b] += y;
            if (a >= 0 && b >= 0) { g[(size_t) a][(size_t) b] -= y; g[(size_t) b][(size_t) a] -= y; }
        };
        const auto r = [&] (int a, int b, double ohms) { add (a, b, 1.0 / std::max (ohms, 1.0)); };

        r (In, -1, R89);       // source series resistor, driven from the right-hand side
        r (In, -1, R24);
        r (In, Rc, R94);
        r (In, A, R28);
        r (U, W, RHigh * (1.0 - high));
        r (W, T, RHigh * high);
        r (W, -1, RPost);
        r (T, M, RLow * low);
        r (M, -1, R95);
        r (M, Mw, RMid * (1.0 - mid));
        r (Mw, -1, RMid * mid);

        const double k = 2.0 * fs;
        for (size_t i = 0; i < caps.size(); ++i)
        {
            capG[i] = k * capValues[i];
            add (caps[i].first, caps[i].second, capG[i]);
        }
        invertMatrix<N> (g, inv);
    }

    double process (double vsrc)
    {
        std::array<double, N> rhs {};
        rhs[In] = vsrc / R89;
        for (size_t i = 0; i < caps.size(); ++i)
        {
            if (caps[i].first >= 0) rhs[(size_t) caps[i].first] += hist[i];
            if (caps[i].second >= 0) rhs[(size_t) caps[i].second] -= hist[i];
        }

        for (size_t r = 0; r < N; ++r)
        {
            double sum = 0.0;
            for (size_t c = 0; c < N; ++c)
                sum += inv[r][c] * rhs[c];
            v[r] = sum;
        }

        for (size_t i = 0; i < caps.size(); ++i)
        {
            const double a = caps[i].first >= 0 ? v[(size_t) caps[i].first] : 0.0;
            const double b = caps[i].second >= 0 ? v[(size_t) caps[i].second] : 0.0;
            hist[i] = 2.0 * capG[i] * (a - b) - hist[i];
        }
        return v[W];
    }

    double inputNode() const { return v[In]; }

private:
    static constexpr std::array<std::pair<int, int>, 4> caps { { { Rc, -1 }, { A, T }, { A, Mw }, { In, U } } };
    static constexpr std::array<double, 4> capValues { C59, C18, C21, CTreble };

    double fs = 192000.0;
    std::array<double, 4> capG {}, hist {};
    std::array<double, N> v {};
    std::array<std::array<double, N>, N> inv {};
};

// Negative feedback path from the output transformer to the phase inverter's tail:
//   feedback - R56 - A - (C12 || Resonance) - B - C30 - Q - R57 - ground
//   B - (C8 + C62) - Presence - ground
class FeedbackNetwork
{
public:
    static constexpr int N = 4;
    enum Node { A, B, Pt, Q };

    static constexpr double R56 = 39e3, R57 = 4.7e3, C12 = 0.0068e-6, CPresence = 0.1e-6 + 0.033e-6, C30 = 22e-6;
    static constexpr double RResonanceMax = 1e6, RPresenceMax = 10e3;

    // Both pots are audio taper. Presence is at minimum resistance fully clockwise,
    // which removes the most feedback and gives the brightest sound.
    static double resonanceOhms (double knob) { return std::max (RResonanceMax * std::pow (knob, 2.5), 100.0); }
    static double presenceOhms (double knob) { return std::max (RPresenceMax * (1.0 - std::pow (knob, 2.5)), 10.0); }

    void prepare (double sampleRate) { fs = sampleRate; reset(); setControls (0.5, 0.5); }
    void reset() { hist = {}; }

    void setControls (double resonance, double presence)
    {
        std::array<std::array<double, N>, N> g {};
        const auto add = [&] (int p, int q, double y)
        {
            if (p >= 0) g[(size_t) p][(size_t) p] += y;
            if (q >= 0) g[(size_t) q][(size_t) q] += y;
            if (p >= 0 && q >= 0) { g[(size_t) p][(size_t) q] -= y; g[(size_t) q][(size_t) p] -= y; }
        };

        add (A, -1, 1.0 / R56);
        add (A, B, 1.0 / resonanceOhms (resonance));
        add (Pt, -1, 1.0 / presenceOhms (presence));
        add (Q, -1, 1.0 / R57);

        const double k = 2.0 * fs;
        for (size_t i = 0; i < caps.size(); ++i)
        {
            capG[i] = k * capValues[i];
            add (caps[i].first, caps[i].second, capG[i]);
        }
        invertMatrix<N> (g, inv);
    }

    // Returns the voltage reaching the tail node Q.
    double process (double vsrc)
    {
        std::array<double, N> rhs {};
        rhs[A] = vsrc / R56;
        for (size_t i = 0; i < caps.size(); ++i)
        {
            if (caps[i].first >= 0) rhs[(size_t) caps[i].first] += hist[i];
            if (caps[i].second >= 0) rhs[(size_t) caps[i].second] -= hist[i];
        }

        std::array<double, N> v {};
        for (size_t r = 0; r < N; ++r)
            for (size_t c = 0; c < N; ++c)
                v[r] += inv[r][c] * rhs[c];

        for (size_t i = 0; i < caps.size(); ++i)
        {
            const double p = caps[i].first >= 0 ? v[(size_t) caps[i].first] : 0.0;
            const double q = caps[i].second >= 0 ? v[(size_t) caps[i].second] : 0.0;
            hist[i] = 2.0 * capG[i] * (p - q) - hist[i];
        }
        return v[Q];
    }

private:
    static constexpr std::array<std::pair<int, int>, 3> caps { { { A, B }, { B, Pt }, { B, Q } } };
    static constexpr std::array<double, 3> capValues { C12, CPresence, C30 };

    double fs = 176400.0;
    std::array<double, 3> capG {}, hist {};
    std::array<std::array<double, N>, N> inv {};
};

// Long-tail-pair inverter with loop feedback injected into the tail.
class PhaseInverter
{
public:
    static constexpr double Rp1 = 82e3, Rp2 = 100e3, R52 = 470.0, R51 = 10e3, R57 = 4.7e3, R56 = 39e3;

    void prepare (double fs)
    {
        T = 1.0 / fs;
        vp1 = railV3 * 0.6; vp2 = railV3 * 0.6; vk = 70.0;
        vc31 = 0.0;
        for (int i = 0; i < 80; ++i)
            solve (0.0, 0.0, true);
        vc31 = tailJ - tailQ;
    }

    void process (double input, double feedback)
    {
        solve (input, feedback, false);
        vc31 += (tailJ - tailQ - vc31) * dcTrack; // C31 charges through its 1M grid return
    }

    double plateA() const { return vp1; }
    double plateB() const { return vp2; }
    double tail() const { return vk; }
    double tailCurrent() const { return current; }

    double dcTrack = 2.0 * pi * 3.4 / 192000.0; // C31 with its 1M return

private:
    void solve (double input, double fb, bool dc)
    {
        // Feedback arrives already scaled by FeedbackNetwork; C30 keeps it out of the DC path.
        const double rq = R57;
        const double fbk = 1.0;
        const double rt = R52 + R51 + rq;

        for (int iter = 0; iter < (dc ? 60 : 10); ++iter)
        {
            const double it = (vk - fbk * fb) / rt;
            const double vj = vk - R52 * it;
            const double q = it * rq + fbk * fb;

            const double vg1 = vj + input;
            const double vg2 = dc ? vj : q + vc31; // C31 ties this grid to the feedback node

            const auto a = triodeCurrent (ax7, vg1 - vk, vp1 - vk);
            const auto b = triodeCurrent (ax7, vg2 - vk, vp2 - vk);

            const double dj = 1.0 - R52 / rt;
            const double dg2 = dc ? dj : rq / rt;
            const double da = (dj - 1.0) * a.dVgk - a.dVpk;
            const double db = (dg2 - 1.0) * b.dVgk - b.dVpk;

            const double f1 = (railV3 - vp1) / Rp1 - a.ip;
            const double f2 = (railV3 - vp2) / Rp2 - b.ip;
            const double f3 = a.ip + b.ip - it;

            const double a11 = -1.0 / Rp1 - a.dVpk, a13 = -da;
            const double a22 = -1.0 / Rp2 - b.dVpk, a23 = -db;
            const double a31 = a.dVpk, a32 = b.dVpk, a33 = da + db - 1.0 / rt;

            // Eliminate the two plate unknowns.
            const double den = a33 - a31 * a13 / a11 - a32 * a23 / a22;
            double dk = (-f3 + a31 * f1 / a11 + a32 * f2 / a22) / den;
            dk = std::clamp (dk, -15.0, 15.0);
            double dp1 = std::clamp ((-f1 - a13 * dk) / a11, -80.0, 80.0);
            double dp2 = std::clamp ((-f2 - a23 * dk) / a22, -80.0, 80.0);

            vk = std::max (vk + dk, 0.0);
            vp1 = std::clamp (vp1 + dp1, 1.0, railV3);
            vp2 = std::clamp (vp2 + dp2, 1.0, railV3);
            current = it;
            tailQ = q;
            tailJ = vj;

            if (std::abs (dk) + std::abs (dp1) + std::abs (dp2) < 2.0e-3)
                break;
        }
    }

    double T = 0.0, vp1 = 280.0, vp2 = 280.0, vk = 70.0, vc31 = 0.0, current = 0.005, tailQ = 0.0, tailJ = 0.0;
};

struct Pentode
{
    // kg1 is lowered from the published 1460 so the output stage reaches the 120 W measured on a real amp.
    static constexpr double mu = 8.7, ex = 1.35, kg1 = 1100.0, kp = 48.0, kvb = 12.0;

    static void plate (double vg, double vp, double& ip, double& dVp)
    {
        double l, dl;
        softPlus (kp * (1.0 / mu + vg / railScreen), l, dl);
        const double e1 = railScreen / kp * l;
        const double base = std::pow (std::max (e1, 1.0e-9), ex) / kg1;
        const double v = std::max (vp, 1.0);
        ip = base * std::atan (v / kvb);
        dVp = base / kvb / (1.0 + v * v / (kvb * kvb));
    }
};

class PowerStage
{
public:
    static constexpr double Zaa = 2900.0;          // Fitted to the 31 V rms into 8 ohm measured at full power
    static constexpr double biasVolts = -58.0;     // Measured on a real amp
    static constexpr double Rleak = 220e3, Rstop = 2200.0, Cc = 0.047e-6;

    void prepare (double fs)
    {
        g = 2.0 * Cc * fs;
        bias = biasVolts;
        x = 0.0;
    }

    void setSourceDc (double dcA, double dcB) { hA = g * dcA; hB = g * dcB; }

    double process (double vA, double vB)
    {
        const double gridA = driveGrid (vA, hA);
        const double gridB = driveGrid (vB, hB);

        for (int iter = 0; iter < 12; ++iter)
        {
            double ia, da, ib, db;
            Pentode::plate (gridA, railPlate - x, ia, da);
            Pentode::plate (gridB, railPlate + x, ib, db);

            // Two tubes per side, with Zaa/4 per side referred to the primary.
            const double load = Zaa * 0.25;
            const double f = x - 2.0 * (ia - ib) * load;
            const double dF = 1.0 + 2.0 * load * (da + db);
            const double dx = std::clamp (-f / dF, -120.0, 120.0);
            x = std::clamp (x + dx, -railPlate + 5.0, railPlate - 5.0);
            if (std::abs (dx) < 1.0e-4)
                break;
        }
        return 2.0 * x;
    }

    double biasVoltage() const { return bias; }

private:
    double driveGrid (double vsrc, double& h) const
    {
        const double rser = 30e3 + 1.0 / g;
        const double voc = (vsrc - h / g) * Rleak / (rser + Rleak);
        const double rth = rser * Rleak / (rser + Rleak);
        const double rg = rth + Rstop;

        double vg = bias + voc;
        double ig = 0.0, dIg = 0.0;
        for (int iter = 0; iter < 10; ++iter)
        {
            gridCurrent (vg, 1000.0, ig, dIg);
            const double f = bias + voc - vg - ig * rg;
            const double dv = f / (1.0 + dIg * rg);
            vg += dv;
            if (std::abs (dv) < 1.0e-4)
                break;
        }
        gridCurrent (vg, 1000.0, ig, dIg);

        const double vnode = vg + ig * Rstop - bias;
        const double i = (vsrc - h / g - vnode) / rser;
        const double vcap = (i + h) / g;
        h = g * vcap + i;
        return vg;
    }

    double g = 1.0, bias = -40.0, x = 0.0, hA = 0.0, hB = 0.0;
};

struct Controls
{
    double pre = 0.5, low = 0.5, mid = 0.5, high = 0.5, post = 0.4, resonance = 0.5, presence = 0.5;
    double rhythmPre = 0.5, rhythmPost = 0.4;
    bool cab = true, lead = true, crunch = false;
};

class Amp
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;

        v1a.configure ({ railV1, 220e3, 1820.0, 1.0e-6, 22e3, 0.1e-6, 1.0e6, 68e3, 0.0, 0.0, 50.0 }, fs, 0.0);
        v1b.configure ({ railV1, 100e3, 1820.0, 1.0e-6, 200e3, 0.022e-6, 1.0e6, 470e3, 0.0, 0.0, 50.0 }, fs, 0.0);
        v2a.configure ({ railV1, 100e3, 39e3, 0.0, 530e3, 0.022e-6, 1.0e6, 0.0, 0.0, 0.0, 2.5 }, fs, v1b.plate());
        v2b.configure ({ railV2, 220e3, 1820.0, 1.0e-6, 70e3, 0.022e-6, 330e3, 220e3, 0.0, 0.0, 55.0 }, fs, v2a.plate());
        v5b.configure ({ railV2, 220e3, 2200.0, 0.0, 1.1e6, 0.022e-6, 100e3, 0.0, 0.0, 0.0, 30.0 }, fs, v2b.plate());
        v5a.configure ({ railV2, 100e3, 2200.0, 0.0, 1.1e6, 0.022e-6, 1.0e6, 0.0, 1.0e6, 0.022e-6, 1.0 }, fs, v5b.plate());
        v3a.configure ({ railV2, 68e3, 3300.0, 0.0047e-6, 1.0e3, 0.047e-6, 1.0e6, 0.0, 0.0, 0.0, 35.0 }, fs, 0.0);

        v1aDc = v1a.plate();
        v1b.reset (0.0);
        v2a.reset (v1b.plate());
        v2b.reset (v2a.plate());
        v5b.reset (v2b.plate());
        v5a.reset (v5b.plate());
        v3a.reset (0.0);

        c48.setLowPass (1.0 / (2.0 * pi * 58e3 * 0.001e-6), fs);
        c48.reset (v2a.plate());
        blockV1.setLowPass (1.0 / (2.0 * pi * 970e3 * 0.022e-6), fs);
        blockV1.reset (v1aDc);
        blockPi.setLowPass (1.0 / (2.0 * pi * 1.1e6 * 0.022e-6), fs);
        blockPi.reset (v3a.plate());

        transformerLow.setLowPass (25.0, fs); // ESTIMATE: output transformer low corner
        transformerLow.reset();

        tone.prepare (fs);
        rhythm.prepare (fs);
        feedback.prepare (fs);
        pi2.prepare (fs);
        pi2.dcTrack = 2.0 * pi * 3.4 / fs;
        power.prepare (fs);
        power.setSourceDc (pi2.plateA(), pi2.plateB());

        speakerLow.setPeak (95.0, 2.0, 5.0, fs);
        speakerHigh.setPeak (6000.0, 0.5, 6.0, fs);
        cabHp.setHighPass (78.0, 0.9, fs);
        cabLow.setPeak (120.0, 1.2, 2.5, fs);
        cabMid.setPeak (2500.0, 1.1, 5.0, fs);
        cabLp1.setLowPass (5200.0, 0.7, fs);
        cabLp2.setLowPass (6200.0, 0.9, fs);

        blendStep = 1.0 / (0.015 * fs);
        activeLead = controls.lead;
        blend = activeLead ? 1.0 : 0.0;
        setControls (controls);
        applyChannelRelays();
        settle();
    }

    void setControls (const Controls& next)
    {
        controls = next;
        tone.setControls (controls.low, controls.mid, controls.high);

        // Pre pot VR2 with 1M || 1n bridging its upper half (R40, C19) and the
        // 470k || 2.2n series feed (R29, C17).
        const double a = std::clamp (std::pow (controls.pre, 2.5), 0.002, 0.998);
        const double top = 1.0e6 * (1.0 - a), bottom = 1.0e6 * a;
        const double a1 = 1.0 / 470e3, b1 = 2.2e-9, a2 = 1.0 / top + 1.0e-6, b2 = 1.0e-9, gb = 1.0 / bottom, l = 1.0e-6;
        preNet.setAnalog (b1 * b2, a1 * b2 + b1 * a2, a1 * a2,
                          b1 * b2, (a1 + l) * b2 + b1 * (a2 + gb) + b2 * gb, (a1 + l) * (a2 + gb) + a2 * gb, fs);
        leadSource = std::max (top * bottom / 1.0e6, 1000.0);

        rhythm.setControls (controls.rhythmPre, controls.crunch);
        const double rt = RhythmNetwork::taper (controls.rhythmPre);
        rhythmSource = std::max (rt * (1.0 - rt) * 1.0e6, 1000.0);

        v1b.setSourceResistance (activeLead ? leadSource : rhythmSource);

        postLead = std::pow (controls.post, 2.5);
        postRhythm = std::pow (controls.rhythmPost, 2.5);

        feedback.setControls (controls.resonance, controls.presence);
    }

    double process (double vin)
    {
        const double p1 = v1a.process (vin, 0.0);
        const double dc = blockV1.lowPass (p1);
        const double xLead = preNet.process (p1 - dc);
        const double xRhythm = rhythm.process (p1 - dc);

        // The output fades through silence while the relays change over.
        const double target = controls.lead ? 1.0 : 0.0;
        if (blend != target)
        {
            blend += std::clamp (target - blend, -blendStep, blendStep);
            if ((blend >= 0.5) != activeLead)
            {
                activeLead = blend >= 0.5;
                applyChannelRelays();
            }
        }
        const double x = activeLead ? xLead : xRhythm;
        const double channelGain = std::abs (2.0 * blend - 1.0);

        const double p2 = v1b.process (x, 0.0);
        // V2B's source resistance already includes this plate's output impedance, so no load current is fed back.
        const double p3 = v2a.process (p2, 0.0);
        const double p4 = v2b.process (c48.lowPass (p3), 0.0);
        const double p5 = v5b.process (p4, 0.0);

        const double fbLoad = (v5a.output() - tone.inputNode()) / ToneNetwork::R89;
        v5a.process (p5, fbLoad);

        const double t = tone.process (v5a.output());
        const double driven = v3a.process (t * (activeLead ? postLead : postRhythm) * channelGain * 0.98, 0.0);
        const double dcPi = blockPi.lowPass (driven);

        const double fb = feedback.process (lastPrimary / feedbackTurns);
        pi2.process ((driven - dcPi) * 0.91, fb);

        const double raw = power.process (pi2.plateA(), pi2.plateB());
        const double primary = raw - transformerLow.lowPass (raw); // the transformer cannot pass DC
        lastPrimary = primary;

        double y = speakerHigh.process (speakerLow.process (primary)) * outputScale;

        if (controls.cab)
            y = cabLp2.process (cabLp1.process (cabMid.process (cabLow.process (cabHp.process (y))))) * 1.4;
        return y;
    }

    const TriodeStage& stage (int index) const
    {
        const TriodeStage* s[] = { &v1a, &v1b, &v2a, &v2b, &v5b, &v5a, &v3a };
        return *s[index];
    }
    const PhaseInverter& inverter() const { return pi2; }
    const PowerStage& powerStage() const { return power; }

private:
    // Relay K1 in rhythm: 22u bypass on V1B's cathode, 220k shunt on V2A's grid.
    void applyChannelRelays()
    {
        v1b.setCathodeCap (activeLead ? 1.0e-6 : 23.0e-6);
        v2a.setGridLeak (activeLead ? 1.0e6 : 1.0e6 * 220e3 / (1.0e6 + 220e3));
        v1b.setSourceResistance (activeLead ? leadSource : rhythmSource);
    }

    void settle()
    {
        for (int i = 0; i < (int) (fs * 0.25); ++i)
            process (0.0);
    }

    static constexpr double feedbackTurns = 13.46; // sqrt (Zaa / 16)
    static constexpr double outputScale = 1.0 / 1400.0; // maps the speaker signal into digital range

    double fs = 192000.0, v1aDc = 0.0, postLead = 0.1, postRhythm = 0.1, lastPrimary = 0.0;
    double blend = 1.0, blendStep = 0.001, leadSource = 200e3, rhythmSource = 200e3;
    bool activeLead = true;
    RhythmNetwork rhythm;
    Controls controls;
    TriodeStage v1a, v1b, v2a, v2b, v5b, v5a, v3a;
    OnePole c48, blockV1, blockPi, transformerLow;
    ToneNetwork tone;
    PhaseInverter pi2;
    PowerStage power;
    FeedbackNetwork feedback;
    Biquad preNet, speakerLow, speakerHigh, cabHp, cabLow, cabMid, cabLp1, cabLp2;
};
} // namespace amp
