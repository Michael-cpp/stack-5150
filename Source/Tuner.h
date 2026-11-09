// Chromatic tuner. The audio thread pushes samples without locking; the message thread runs
// the YIN pitch detector on the most recent ones.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tools
{
struct TunerReading
{
    bool valid = false;
    double hz = 0.0, cents = 0.0;
    int midi = 0;
};

inline const char* noteName (int midi)
{
    static const char* const names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((midi % 12) + 12) % 12];
}

inline int noteOctave (int midi) { return (int) std::floor (midi / 12.0) - 1; }

class Tuner
{
public:
    static constexpr double minHz = 30.0, maxHz = 1500.0;

    void prepare (double sampleRate)
    {
        const int f = std::max (1, (int) std::lround (sampleRate / 24000.0));
        factor = f;
        accumulator = 0.0f;
        accumulated = 0;
        rate.store (sampleRate / f, std::memory_order_release);
    }

    // Audio thread.
    void push (const float* samples, int count) noexcept
    {
        auto at = written.load (std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
        {
            accumulator += samples[i];
            if (++accumulated < factor)
                continue;
            ring[(size_t) (at & mask)].store (accumulator / (float) factor, std::memory_order_relaxed);
            ++at;
            accumulator = 0.0f;
            accumulated = 0;
        }
        written.store (at, std::memory_order_release);
    }

    // Message thread.
    TunerReading analyse (double a4)
    {
        const double fs = rate.load (std::memory_order_acquire);
        double hz = 0.0;
        const bool found = fs > 0.0 && detect (fs, hz);

        if (found)
        {
            if (! history.empty() && std::abs (12.0 * std::log2 (hz / median())) > 0.5)
                history.clear();
            history.push_back (hz);
            if (history.size() > 5)
                history.erase (history.begin());
            misses = 0;
        }
        else if (++misses >= 3)
        {
            history.clear();
        }

        TunerReading r;
        if (history.empty())
            return r;

        r.valid = true;
        r.hz = median();
        const double note = 69.0 + 12.0 * std::log2 (r.hz / a4);
        r.midi = (int) std::lround (note);
        r.cents = 100.0 * (note - r.midi);
        return r;
    }

private:
    static constexpr std::uint64_t ringSize = 8192, mask = ringSize - 1;

    double median() const
    {
        auto sorted = history;
        std::sort (sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    bool detect (double fs, double& hz)
    {
        const int maxTau = (int) std::ceil (fs / minHz);
        const int minTau = std::max (2, (int) std::floor (fs / maxHz));
        const int window = maxTau * 3 / 2;
        const int total = window + maxTau + 2;
        const auto end = written.load (std::memory_order_acquire);
        if (end < (std::uint64_t) total)
            return false;

        x.resize ((size_t) total);
        double mean = 0.0;
        for (int i = 0; i < total; ++i)
        {
            x[(size_t) i] = ring[(size_t) ((end - (std::uint64_t) total + (std::uint64_t) i) & mask)].load (std::memory_order_relaxed);
            mean += x[(size_t) i];
        }
        mean /= total;

        double energy = 0.0;
        for (auto& v : x)
        {
            v -= mean;
            energy += v * v;
        }
        if (std::sqrt (energy / total) < 2.0e-4)
            return false;

        // Cumulative mean normalised difference.
        d.assign ((size_t) maxTau + 2, 1.0);
        double running = 0.0;
        for (int tau = 1; tau <= maxTau + 1; ++tau)
        {
            double sum = 0.0;
            for (int j = 0; j < window; ++j)
            {
                const double diff = x[(size_t) j] - x[(size_t) (j + tau - 1 < total ? j + tau : total - 1)];
                sum += diff * diff;
            }
            running += sum;
            d[(size_t) tau] = running > 0.0 ? sum * tau / running : 1.0;
        }

        // The first dip under the threshold avoids locking on to a lower octave.
        int best = -1;
        for (int tau = minTau; tau <= maxTau; ++tau)
        {
            if (d[(size_t) tau] >= 0.12)
                continue;
            while (tau < maxTau && d[(size_t) tau + 1] < d[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
        if (best < 0)
        {
            best = minTau;
            for (int tau = minTau; tau <= maxTau; ++tau)
                if (d[(size_t) tau] < d[(size_t) best])
                    best = tau;
            if (d[(size_t) best] > 0.3 || best == minTau || best == maxTau)
                return false;
        }

        double tau = interpolatedDip (best);

        // High notes span few samples per period, so time as many whole periods as fit.
        const int periods = (int) std::floor ((maxTau - 2) / tau);
        if (periods > 1)
        {
            const int centre = (int) std::lround (periods * tau);
            int dip = centre;
            for (int t = std::max (2, centre - 2); t <= std::min (maxTau, centre + 2); ++t)
                if (d[(size_t) t] < d[(size_t) dip])
                    dip = t;
            if (d[(size_t) dip] < 0.3 && dip > 1 && dip < maxTau + 1)
                tau = interpolatedDip (dip) / periods;
        }

        hz = fs / tau;
        return hz >= minHz && hz <= maxHz;
    }

    double interpolatedDip (int at) const
    {
        const double a = d[(size_t) at - 1], b = d[(size_t) at], c = d[(size_t) at + 1];
        const double curve = a - 2.0 * b + c;
        return at + (std::abs (curve) > 1.0e-12 ? 0.5 * (a - c) / curve : 0.0);
    }

    std::array<std::atomic<float>, (size_t) ringSize> ring {};
    std::atomic<std::uint64_t> written { 0 };
    std::atomic<double> rate { 0.0 };
    int factor = 1, accumulated = 0, misses = 0;
    float accumulator = 0.0f;
    std::vector<double> x, d, history;
};
} // namespace tools
