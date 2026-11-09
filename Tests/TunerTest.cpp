// Accuracy of the tuner on synthetic notes. Exit code is the number of failures.
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Tuner.h"

static int failures = 0;

static void check (const char* name, bool ok)
{
    std::printf ("%-58s %s\n", name, ok ? "ok" : "FAIL");
    failures += ok ? 0 : 1;
}

static tools::TunerReading measure (double fs, double hz, double level, double a4 = 440.0, double harmonics = 8)
{
    auto tuner = std::make_unique<tools::Tuner>();
    tuner->prepare (fs);
    std::mt19937 rng (1);
    std::normal_distribution<float> noise (0.0f, (float) level * 0.002f);
    const double pi = 3.14159265358979323846;
    std::vector<float> block (512);
    tools::TunerReading r;
    double t = 0.0;
    for (int b = 0; b < 40; ++b)
    {
        for (auto& s : block)
        {
            double v = 0.0;
            for (int h = 1; h <= (int) harmonics && h * hz < fs * 0.45; ++h)
                v += std::sin (2.0 * pi * hz * h * t + 0.7 * h) / h;
            s = (float) (level * v) + noise (rng);
            t += 1.0 / fs;
        }
        tuner->push (block.data(), (int) block.size());
        if (b > 20)
            r = tuner->analyse (a4);
    }
    return r;
}

int main()
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        double worst = 0.0;
        int wrong = 0;
        for (int midi = 28; midi <= 88; ++midi) // low E to the 24th fret of the high E
        {
            const auto r = measure (fs, 440.0 * std::pow (2.0, (midi - 69) / 12.0), 0.05);
            if (! r.valid || r.midi != midi)
                ++wrong;
            else
                worst = std::max (worst, std::abs (r.cents));
        }
        std::printf ("%.0f Hz: wrong notes %d, worst error %.3f cents\n", fs, wrong, worst);
        check ("every note from E1 to E6 is named correctly", wrong == 0);
        check ("worst error under 1 cent", worst < 1.0);
    }

    const auto lowB = measure (48000.0, 30.87, 0.05);
    check ("drop-tuned B0 is found", lowB.valid && lowB.midi == 23);

    const auto sharp = measure (48000.0, 110.0 * std::pow (2.0, 10.0 / 1200.0), 0.05);
    check ("A2 ten cents sharp reads +10", sharp.valid && sharp.midi == 45 && std::abs (sharp.cents - 10.0) < 0.5);

    const auto flat = measure (48000.0, 110.0 * std::pow (2.0, -25.0 / 1200.0), 0.05);
    check ("A2 25 cents flat reads -25", flat.valid && flat.midi == 45 && std::abs (flat.cents + 25.0) < 0.5);

    const auto ref = measure (48000.0, 432.0, 0.05, 432.0);
    check ("A4 reference of 432 Hz reads in tune", ref.valid && ref.midi == 69 && std::abs (ref.cents) < 0.5);

    const auto quiet = measure (48000.0, 82.41, 0.002);
    check ("quiet DI level is detected", quiet.valid && quiet.midi == 40);

    const auto silent = measure (48000.0, 82.41, 0.0);
    check ("silence gives no reading", ! silent.valid);

    check ("note names", std::string (tools::noteName (40)) == "E" && tools::noteOctave (40) == 2 && std::string (tools::noteName (61)) == "C#");

    std::printf ("%d failure(s)\n", failures);
    return failures;
}
