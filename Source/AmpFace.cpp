#include "AmpFace.h"

#include <algorithm>
#include <vector>

namespace face
{
namespace
{
struct Field
{
    Field (int width, int height) : w (width), h (height), v ((size_t) (width * height), 0.0f) {}
    float& at (int x, int y) { return v[(size_t) (y * w + x)]; }
    float get (int x, int y) const { return v[(size_t) (juce::jlimit (0, h - 1, y) * w + juce::jlimit (0, w - 1, x))]; }
    int w, h;
    std::vector<float> v;
};

void blurLine (const float* src, float* dst, int n, int stride, int r)
{
    const float norm = 1.0f / (float) (2 * r + 1);
    float sum = 0.0f;
    for (int i = -r; i <= r; ++i)
        sum += src[juce::jlimit (0, n - 1, i) * stride];
    for (int i = 0; i < n; ++i)
    {
        dst[i * stride] = sum * norm;
        sum += src[juce::jlimit (0, n - 1, i + r + 1) * stride] - src[juce::jlimit (0, n - 1, i - r) * stride];
    }
}

void blur (Field& f, int radius)
{
    if (radius <= 0)
        return;
    std::vector<float> tmp (f.v.size());
    for (int pass = 0; pass < 3; ++pass)
    {
        for (int y = 0; y < f.h; ++y)
            blurLine (f.v.data() + y * f.w, tmp.data() + y * f.w, f.w, 1, radius);
        for (int x = 0; x < f.w; ++x)
            blurLine (tmp.data() + x, f.v.data() + x, f.h, f.w, radius);
    }
}

template <typename Draw>
Field paintMask (int w, int h, float s, Draw&& draw)
{
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (s));
        g.setColour (juce::Colours::white);
        draw (g);
    }
    Field f (w, h);
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f.at (x, y) = (float) ((const juce::PixelARGB*) data.getPixelPointer (x, y))->getAlpha() / 255.0f;
    return f;
}

class ValueNoise
{
public:
    explicit ValueNoise (juce::int64 seed)
    {
        juce::Random r (seed);
        for (auto& v : table)
            v = r.nextFloat();
    }
    float eval (float x, float y) const
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        const float fx = smooth (x - (float) xi), fy = smooth (y - (float) yi);
        const float a = at (xi, yi) + fx * (at (xi + 1, yi) - at (xi, yi));
        const float b = at (xi, yi + 1) + fx * (at (xi + 1, yi + 1) - at (xi, yi + 1));
        return a + fy * (b - a);
    }
    float fractal (float x, float y) const
    {
        return 0.55f * eval (x, y) + 0.30f * eval (x * 2.1f, y * 2.1f) + 0.15f * eval (x * 4.3f, y * 4.3f);
    }

private:
    static float smooth (float t) { return t * t * (3.0f - 2.0f * t); }
    float at (int x, int y) const { return table[(size_t) ((x & 255) + ((y & 255) << 8))]; }
    std::array<float, 65536> table;
};

float smoothstep (float a, float b, float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

float boxDistance (float x, float y, const juce::Rectangle<float>& r, float radius)
{
    const float qx = std::abs (x - r.getCentreX()) - r.getWidth() * 0.5f + radius;
    const float qy = std::abs (y - r.getCentreY()) - r.getHeight() * 0.5f + radius;
    return std::hypot (juce::jmax (qx, 0.0f), juce::jmax (qy, 0.0f)) + juce::jmin (juce::jmax (qx, qy), 0.0f) - radius;
}

// Distance from the centre of the nearest honeycomb cell; 0.5 is the cell edge.
float hexDistance (float x, float y)
{
    constexpr float rx = 1.0f, ry = 1.7320508f;
    const auto wrap = [] (float v, float m) { return v - m * std::floor (v / m); };
    const float ax = wrap (x, rx) - rx * 0.5f, ay = wrap (y, ry) - ry * 0.5f;
    const float bx = wrap (x - rx * 0.5f, rx) - rx * 0.5f, by = wrap (y - ry * 0.5f, ry) - ry * 0.5f;
    const bool first = ax * ax + ay * ay < bx * bx + by * by;
    const float gx = std::abs (first ? ax : bx), gy = std::abs (first ? ay : by);
    return juce::jmax (gx, gx * 0.5f + gy * 0.8660254f);
}

juce::Font faceFont (float size, bool bold = true)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Point<float> polar (juce::Point<float> c, float r, float a)
{
    return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
}

const juce::Rectangle<float> bodyArea (8.0f, 8.0f, 1184.0f, 400.0f);
const juce::Rectangle<float> grilleArea (52.0f, 54.0f, 1096.0f, 222.0f);
const juce::Rectangle<float> stripArea (52.0f, 280.0f, 1096.0f, 102.0f);

void drawStripText (juce::Graphics& g, const Layout& l)
{
    const char* below[] = { "PRE GAIN", "PRE GAIN", "LOW", "MID", "HIGH", "POST GAIN", "POST GAIN", "RESONANCE", "PRESENCE" };
    const char* above[] = { "RHYTHM", "LEAD", "", "", "", "RHYTHM", "LEAD", "", "" };

    for (size_t i = 0; i < l.knobs.size(); ++i)
    {
        const auto b = l.knobs[i].toFloat();
        const auto c = b.getCentre();
        const int cx = (int) c.x;

        g.setFont (faceFont (10.5f));
        for (int t = 0; t <= 10; ++t)
        {
            const float a = rotaryStart + (rotaryEnd - rotaryStart) * (float) t / 10.0f;
            const auto p = polar (c, b.getWidth() * 0.5f + 7.0f, a);
            g.drawText (juce::String (t), juce::Rectangle<float> (p.x - 9.0f, p.y - 6.0f, 18.0f, 12.0f), juce::Justification::centred);
        }
        g.setFont (faceFont (11.5f));
        g.drawText (below[i], cx - 42, (int) stripArea.getBottom() - 14, 84, 12, juce::Justification::centred);
        g.drawText (above[i], cx - 42, (int) stripArea.getY() + 2, 84, 12, juce::Justification::centred);
    }

    const float top = stripArea.getY() + 5.0f, bottom = stripArea.getBottom() - 5.0f;
    for (float x : { 150.0f, 282.0f, 370.0f, 604.0f, 768.0f, 934.0f })
        g.drawLine (x, top, x, bottom, 1.2f);

    const auto title = [&] (float x0, float x1, const char* text)
    {
        const float y = stripArea.getY() + 8.0f, mid = (x0 + x1) * 0.5f;
        g.setFont (faceFont (11.5f));
        g.drawText (text, juce::Rectangle<float> (mid - 50.0f, y - 6.0f, 100.0f, 12.0f), juce::Justification::centred);
        g.drawLine (x0 + 8.0f, y, mid - 44.0f, y, 1.0f);
        g.drawLine (mid + 44.0f, y, x1 - 8.0f, y, 1.0f);
    };
    title (370.0f, 604.0f, "EQUALIZATION");
    title (768.0f, 934.0f, "POWER AMP");

    g.setFont (faceFont (10.5f));
    g.drawText ("INPUTS", 56, 288, 90, 14, juce::Justification::centred);
    g.setFont (faceFont (9.5f));
    g.drawText ("HIGH", 56, 356, 44, 10, juce::Justification::centred);
    g.drawText ("GAIN", 56, 366, 44, 10, juce::Justification::centred);
    g.drawText ("NORMAL", 100, 356, 48, 10, juce::Justification::centred);
    g.drawText ("GAIN", 100, 366, 48, 10, juce::Justification::centred);

    g.setFont (faceFont (9.5f));
    const auto ch = l.channelButton, cr = l.crunchButton;
    g.drawText ("CHANNEL", ch.getCentreX() - 34, ch.getY() - 12, 68, 10, juce::Justification::centred);
    g.drawText ("CRUNCH", cr.getCentreX() - 34, cr.getBottom() + 2, 68, 10, juce::Justification::centred);

    g.setFont (faceFont (10.5f));
    g.drawText ("ON", 1040, 294, 40, 12, juce::Justification::centred);
    g.drawText ("ON", 1098, 294, 40, 12, juce::Justification::centred);
    g.setFont (faceFont (9.5f));
    g.drawText ("STANDBY", 1026, 355, 68, 13, juce::Justification::centred);
    g.drawText ("POWER", 1090, 355, 56, 13, juce::Justification::centred);
    g.setFont (faceFont (9.0f));
    g.drawText ("STANDBY", 938, 288, 48, 11, juce::Justification::centred);
    g.drawText ("POWER", 986, 288, 42, 11, juce::Justification::centred);
    g.setFont (faceFont (12.0f));
    g.drawText ("STACK 5150", 938, 332, 92, 16, juce::Justification::centred);
}

void drawPlates (juce::Graphics& g)
{
    g.setFont (faceFont (138.0f));
    g.drawText ("5150", 330, 86, 470, 140, juce::Justification::centred);
    g.setFont (faceFont (54.0f));
    g.drawText ("STACK", 866, 210, 260, 60, juce::Justification::centred);
}

void strokeScratch (juce::Graphics& g, juce::Random& r, float x, float y, float angle, float length, float width)
{
    const float bend = (r.nextFloat() - 0.5f) * length * 0.2f;
    juce::Path p;
    p.startNewSubPath (x, y);
    p.quadraticTo (x + std::cos (angle) * length * 0.5f - std::sin (angle) * bend,
                   y + std::sin (angle) * length * 0.5f + std::cos (angle) * bend,
                   x + std::cos (angle) * length, y + std::sin (angle) * length);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawOccluders (juce::Graphics& g, const Layout& l, float dx, float dy)
{
    for (const auto& k : l.knobs)
        g.fillEllipse (k.toFloat().reduced (5.0f).translated (dx, dy));
    g.fillRoundedRectangle (l.channelButton.toFloat().translated (dx * 0.4f, dy * 0.4f), 2.0f);
    g.fillRoundedRectangle (l.crunchButton.toFloat().translated (dx * 0.4f, dy * 0.4f), 2.0f);
    for (float x : { 78.0f, 124.0f })
        g.fillEllipse (x - 15.0f + dx * 0.3f, 325.0f - 15.0f + dy * 0.3f, 30.0f, 30.0f);
    for (float x : { 1060.0f, 1118.0f })
        g.fillRoundedRectangle (x - 13.0f + dx * 0.4f, 308.0f + dy * 0.4f, 26.0f, 44.0f, 2.0f);
}

void drawJack (juce::Graphics& g, float x, float y)
{
    const float lit = juce::jlimit (0.25f, 1.0f, lightAt (x, y) * 1.35f);
    const float lightDir = std::atan2 (lightX - x, -(lightY - y));
    const auto twoPi = juce::MathConstants<float>::twoPi;

    // Machined metal reflects as bright and dark bands around its circular grain.
    const auto ring = [&] (float outer, float inner, float shift, float level, juce::Colour tint)
    {
        const int sectors = 96;
        for (int i = 0; i < sectors; ++i)
        {
            const float a0 = twoPi * (float) i / (float) sectors, a1 = twoPi * (float) (i + 1) / (float) sectors;
            const float c = std::cos (2.0f * (a0 - lightDir - shift));
            const float v = juce::jlimit (0.0f, 1.0f, level * (0.30f + 0.70f * c * c * c * c) * (0.4f + 0.8f * lit));
            juce::Path p;
            p.addPieSegment (x - outer, y - outer, outer * 2.0f, outer * 2.0f, a0, a1 + 0.01f, inner / outer);
            g.setColour (tint.withMultipliedBrightness (v));
            g.fillPath (p);
        }
    };

    g.setColour (juce::Colour (0xff050505));
    g.fillEllipse (x - 16.5f, y - 16.5f, 33.0f, 33.0f);

    const auto brass = juce::Colour::fromFloatRGBA (0.93f, 0.89f, 0.78f, 1.0f);
    const auto nickel = juce::Colour::fromFloatRGBA (0.94f, 0.92f, 0.86f, 1.0f);

    ring (15.5f, 10.5f, 0.0f, 1.0f, brass);            // flat washer
    ring (10.8f, 8.0f, 0.5f, 1.25f, nickel);           // threaded collar

    // Edge of the washer catches the light on top and falls off underneath.
    g.setColour (juce::Colours::white.withAlpha (0.55f * lit));
    juce::Path top;
    top.addCentredArc (x, y, 15.2f, 15.2f, 0.0f, lightDir - 0.95f, lightDir + 0.95f, true);
    g.strokePath (top, juce::PathStrokeType (0.9f));
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    juce::Path low;
    low.addCentredArc (x, y, 15.2f, 15.2f, 0.0f, lightDir + 2.2f, lightDir + 4.1f, true);
    g.strokePath (low, juce::PathStrokeType (1.0f));

    // Step down into the socket.
    g.setColour (juce::Colour (0xff0b0a08));
    g.fillEllipse (x - 8.2f, y - 8.2f, 16.4f, 16.4f);
    ring (7.4f, 5.6f, 1.2f, 0.62f, nickel);            // inner sleeve

    g.setColour (juce::Colour (0xff010101));
    g.fillEllipse (x - 5.6f, y - 5.6f, 11.2f, 11.2f);

    // A glint from the tip contact deep in the socket.
    g.setColour (juce::Colour (0xffb9a97e).withAlpha (0.45f * lit));
    juce::Path contact;
    contact.addCentredArc (x + 0.4f, y + 0.8f, 3.2f, 3.2f, 0.0f, lightDir + 2.4f, lightDir + 3.7f, true);
    g.strokePath (contact, juce::PathStrokeType (0.9f));

    // Tarnish and plug wear on the washer.
    juce::Random wear ((juce::int64) (x * 7.0f));
    for (int i = 0; i < 90; ++i)
    {
        const float a = wear.nextFloat() * twoPi, rr = 9.5f + wear.nextFloat() * 5.8f;
        const float px = x + rr * std::sin (a), py = y - rr * std::cos (a), size = 0.25f + wear.nextFloat() * 0.65f;
        g.setColour ((wear.nextFloat() < 0.75f ? juce::Colour (0xff2a1c0c) : juce::Colours::white).withAlpha (0.10f + wear.nextFloat() * 0.34f));
        g.fillEllipse (px - size, py - size, size * 2.0f, size * 2.0f);
    }
    for (int i = 0; i < 9; ++i)
    {
        const float a = wear.nextFloat() * twoPi;
        juce::Path scuff;
        scuff.addCentredArc (x, y, 9.8f + wear.nextFloat() * 5.0f, 9.8f + wear.nextFloat() * 5.0f, 0.0f, a, a + 0.3f + wear.nextFloat() * 0.8f, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f + wear.nextFloat() * 0.18f));
        g.strokePath (scuff, juce::PathStrokeType (0.45f));
    }
}

} // namespace

juce::Image render (const Layout& l, float s, bool tubesLit)
{
    const int w = juce::roundToInt ((float) l.width * s), h = juce::roundToInt ((float) l.height * s);

    Field fine = paintMask (w, h, s, [&] (juce::Graphics& g)
    {
        juce::Random r (5150);
        for (int i = 0; i < 1500; ++i)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f + r.nextFloat() * 0.45f));
            strokeScratch (g, r, r.nextFloat() * (float) l.width, r.nextFloat() * (float) l.height,
                           (r.nextBool() ? 0.15f : 2.6f) + (r.nextFloat() - 0.5f) * 1.2f,
                           6.0f + r.nextFloat() * r.nextFloat() * 130.0f, 0.3f + r.nextFloat() * 0.35f);
        }
    });

    Field deep = paintMask (w, h, s, [&] (juce::Graphics& g)
    {
        juce::Random r (1992);
        for (int i = 0; i < 46; ++i)
            strokeScratch (g, r, r.nextFloat() * (float) l.width, r.nextFloat() * (float) l.height,
                           r.nextFloat() * 3.14f, 5.0f + r.nextFloat() * 60.0f, 0.5f + r.nextFloat() * 0.9f);
    });

    Field ink = paintMask (w, h, s, [&] (juce::Graphics& g) { drawStripText (g, l); });
    Field plates = paintMask (w, h, s, [&] (juce::Graphics& g) { drawPlates (g); });
    blur (plates, 1);

    Field cast = paintMask (w, h, s, [&] (juce::Graphics& g) { drawOccluders (g, l, 8.0f, 12.0f); });
    blur (cast, juce::roundToInt (5.0f * s));
    Field contact = paintMask (w, h, s, [&] (juce::Graphics& g) { drawOccluders (g, l, 0.0f, 0.0f); });
    blur (contact, juce::roundToInt (2.0f * s));

    Field pebble (w, h);
    {
        juce::Random r (4151);
        for (auto& v : pebble.v)
            v = r.nextFloat() - 0.5f;
        Field wide = pebble;
        blur (pebble, juce::roundToInt (1.6f * s));
        blur (wide, juce::roundToInt (3.4f * s));
        float variance = 0.0f;
        for (size_t i = 0; i < pebble.v.size(); ++i)
        {
            pebble.v[i] = pebble.v[i] - 0.6f * wide.v[i];
            variance += pebble.v[i] * pebble.v[i];
        }
        const float k = 1.0f / std::sqrt (variance / (float) pebble.v.size());
        for (auto& v : pebble.v)
            v *= k;
    }

    // Brushing on the aluminium runs horizontally: streaks vary by row, slowly along it.
    std::vector<float> rows ((size_t) h);
    {
        juce::Random r (606);
        for (auto& v : rows)
            v = r.nextFloat();
        std::vector<float> smooth (rows.size());
        for (int i = 0; i < h; ++i)
            smooth[(size_t) i] = 0.6f * rows[(size_t) i] + 0.4f * rows[(size_t) juce::jmin (h - 1, i + 1)];
        rows.swap (smooth);
    }

    const ValueNoise wearNoise (41), stainNoise (97), fineNoise (5);
    juce::Random grain (31);
    Field height (w, h);

    const auto corner = [&] (float x, float y)
    {
        const auto& b = bodyArea;
        const float cx = juce::jmin (x - b.getX(), b.getRight() - x), cy = juce::jmin (y - b.getY(), b.getBottom() - y);
        return cx < 42.0f && cy < 42.0f && (cx < 30.0f || cy < 30.0f);
    };

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const float lx = ((float) x + 0.5f) / s, ly = ((float) y + 0.5f) / s;
            const float d = boxDistance (lx, ly, bodyArea, 7.0f);
            if (d > 2.0f)
                continue;

            const float bevel = 6.0f, t = juce::jlimit (0.0f, 1.0f, -d / bevel);
            float hh = bevel * s * std::sqrt (juce::jmax (0.0f, 1.0f - (1.0f - t) * (1.0f - t)));

            if (grilleArea.contains (lx, ly))
            {
                const float hd = hexDistance (lx / 11.0f, ly / 11.0f);
                hh += (0.5f - smoothstep (0.37f, 0.45f, hd)) * 2.0f * s - 2.0f * s;
                hh -= 0.5f * fine.at (x, y);
            }
            else if (stripArea.contains (lx, ly))
            {
                hh -= 0.18f * rows[(size_t) y] * s + 0.5f * fine.at (x, y) + 1.2f * deep.at (x, y);
            }
            else if (corner (lx, ly))
            {
                const float rust = stainNoise.fractal (lx * 0.16f, ly * 0.16f);
                hh += 1.6f * s + 1.0f * rust * s - 1.0f * deep.at (x, y);
            }
            else
            {
                hh += (0.09f * juce::jmin (std::abs (pebble.at (x, y)), 1.3f) + 0.05f * pebble.at (x, y)) * s - 0.3f * fine.at (x, y) - 0.7f * deep.at (x, y);
            }
            height.at (x, y) = hh;
        }
    }

    // Tubes behind the grille, with their heaters lit.
    struct Tube { float x, radius; };
    const Tube tubes[] = { { 360.0f, 34.0f }, { 450.0f, 34.0f }, { 660.0f, 34.0f }, { 750.0f, 34.0f },
                           { 180.0f, 19.0f }, { 228.0f, 19.0f }, { 276.0f, 19.0f } };

    juce::Image image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const float lx = ((float) x + 0.5f) / s, ly = ((float) y + 0.5f) / s;
                const float d = boxDistance (lx, ly, bodyArea, 7.0f);
                const float inside = juce::jlimit (0.0f, 1.0f, 0.5f - d * s);

                const float nx0 = height.get (x - 1, y) - height.get (x + 1, y);
                const float ny0 = height.get (x, y - 1) - height.get (x, y + 1);
                const float nLen = 1.0f / std::sqrt (nx0 * nx0 + ny0 * ny0 + 4.0f);
                const float nx = nx0 * nLen, ny = ny0 * nLen, nz = 2.0f * nLen;

                const float ldx = lightX - lx, ldy = lightY - ly;
                const float dist2 = ldx * ldx + ldy * ldy + lightZ * lightZ;
                const float inv = 1.0f / std::sqrt (dist2);
                const float lvx = ldx * inv, lvy = ldy * inv, lvz = lightZ * inv;
                const float att = lightZ * lightZ / dist2;
                const float ndl = juce::jmax (0.0f, nx * lvx + ny * lvy + nz * lvz);
                const float hx = lvx, hy = lvy, hz = lvz + 1.0f, hn = 1.0f / std::sqrt (hx * hx + hy * hy + hz * hz);
                const float ndh = juce::jmax (0.0f, (nx * hx + ny * hy + nz * hz) * hn);

                float r = 0.012f, g = 0.012f, b = 0.013f, gloss = 0.03f, power = 16.0f;
                float emitR = 0.0f, emitG = 0.0f, emitB = 0.0f;
                float occlude = 1.0f;

                if (grilleArea.contains (lx, ly))
                {
                    const float hd = hexDistance (lx / 11.0f, ly / 11.0f);
                    const float hole = 1.0f - smoothstep (0.375f, 0.42f, hd);

                    // What is seen through the holes: chassis, lit tubes, lettering plates.
                    float sr = 0.006f, sg = 0.005f, sb = 0.004f;
                    for (const auto& tube : tubes)
                    {
                        const float dx = (lx - tube.x) / tube.radius;
                        if (std::abs (dx) < 1.0f && ly > 74.0f && ly < 262.0f)
                        {
                            const float fade = smoothstep (74.0f, 104.0f, ly) * (1.0f - smoothstep (232.0f, 262.0f, ly)); const float glass = (0.006f + 0.035f * std::pow (std::abs (dx), 7.0f)) * fade;
                            const float vert = std::exp (-std::pow ((ly - 165.0f) / 62.0f, 2.0f));
                            const float heater = std::exp (-dx * dx * 16.0f) * vert * (tubesLit ? 0.20f : 0.0f);
                            sr += glass + heater;
                            sg += glass * 0.9f + heater * 0.24f;
                            sb += glass * 0.8f + heater * 0.035f;
                        }
                    }
                    const float plate = plates.at (x, y);
                    const float pl = 0.34f * (0.55f + 1.0f * att);
                    sr += (pl - sr) * plate;
                    sg += (pl - sg) * plate;
                    sb += (pl * 0.97f - sb) * plate;

                    emitR = sr * hole;
                    emitG = sg * hole;
                    emitB = sb * hole;

                    r = g = b = 0.028f + 0.020f * stainNoise.eval (lx * 0.25f, ly * 0.25f);
                    r *= 1.0f - hole; g *= 1.0f - hole; b *= 1.0f - hole;
                    gloss = 0.20f * (1.0f - hole);
                    power = 36.0f;

                    // Shadow thrown into the recess by the top of the cabinet.
                    occlude *= 1.0f - 0.72f * std::exp (-(ly - grilleArea.getY()) / 16.0f);
                    occlude *= 1.0f - 0.45f * std::exp (-(lx - grilleArea.getX()) / 12.0f);
                    occlude *= 1.0f - 0.45f * std::exp (-(grilleArea.getRight() - lx) / 12.0f);
                }
                else if (stripArea.contains (lx, ly))
                {
                    const float streak = rows[(size_t) y] * 0.5f + fineNoise.eval (lx * 0.02f, (float) y * 3.0f) * 0.5f;
                    const float base = 0.27f + 0.09f * streak;
                    r = g = b = base;
                    b *= 1.01f;

                    // Brown tarnish patches, heaviest toward the middle of the strip.
                    const float stain = 0.4f * smoothstep (0.55f, 0.82f, stainNoise.fractal (lx * 0.012f, ly * 0.05f));
                    const float grime = smoothstep (0.6f, 0.8f, stainNoise.fractal (lx * 0.09f, ly * 0.14f)) * 0.25f;
                    r *= 1.0f - 0.34f * stain - 0.25f * grime;
                    g *= 1.0f - 0.46f * stain - 0.34f * grime;
                    b *= 1.0f - 0.62f * stain - 0.42f * grime;

                    const float scratch = juce::jmin (1.0f, fine.at (x, y) + deep.at (x, y));
                    r += (0.42f - r) * scratch * 0.6f;
                    g += (0.42f - g) * scratch * 0.6f;
                    b += (0.40f - b) * scratch * 0.6f;

                    const float flake = smoothstep (0.68f, 0.82f, wearNoise.eval (lx * 0.6f, ly * 0.6f)); const float fleck = smoothstep (0.86f, 0.90f, fineNoise.eval (lx * 0.9f, ly * 0.9f)) * smoothstep (0.45f, 0.7f, wearNoise.fractal (lx * 0.03f, ly * 0.07f)); r += (0.75f - r) * fleck; g += (0.75f - g) * fleck; b += (0.72f - b) * fleck;
                    const float printing = juce::jmin (1.0f, ink.at (x, y) * 1.25f) * (1.0f - 0.5f * scratch) * (1.0f - 0.22f * flake);
                    r += (0.002f - r) * printing;
                    g += (0.002f - g) * printing;
                    b += (0.003f - b) * printing;

                    gloss = (0.32f - 0.12f * stain) * (1.0f - 0.95f * printing);
                    power = 22.0f;
                }
                else if (corner (lx, ly))
                {
                    const auto& bb = bodyArea;
                    const float cx = juce::jmin (lx - bb.getX(), bb.getRight() - lx), cy = juce::jmin (ly - bb.getY(), bb.getBottom() - ly);
                    const float edge = juce::jmin (cx, cy);
                    const float n = wearNoise.fractal (lx * 0.14f, ly * 0.14f);
                    const float loss = smoothstep (0.66f, 0.73f, n + 0.27f * std::exp (-edge / 5.0f) - 0.04f);

                    r = 0.018f; g = 0.018f; b = 0.02f;
                    gloss = 0.16f; power = 32.0f;

                    const float rustT = stainNoise.fractal (lx * 0.3f, ly * 0.3f);
                    const float rr = 0.12f + 0.14f * rustT, rg = 0.05f + 0.05f * rustT, rb = 0.02f;
                    const float metal = smoothstep (0.65f, 0.76f, rustT);
                    r += ((rr + (0.42f - rr) * metal) - r) * loss;
                    g += ((rg + (0.42f - rg) * metal) - g) * loss;
                    b += ((rb + (0.40f - rb) * metal) - b) * loss;
                    gloss += 0.25f * loss * metal;
                }
                else if (inside > 0.0f)
                {
                    // Vinyl covering, rubbed pale at the edges, with some tears to the cloth backing.
                    const float e = juce::jmin (juce::jmin (lx - bodyArea.getX(), bodyArea.getRight() - lx),
                                                juce::jmin (ly - bodyArea.getY(), bodyArea.getBottom() - ly));
                    const float nearGrille = juce::jmin (std::abs (ly - grilleArea.getY()), std::abs (ly - stripArea.getBottom()));
                    const float rub = smoothstep (0.45f, 0.82f, wearNoise.fractal (lx * 0.05f, ly * 0.16f)
                                                                     + 0.25f * std::exp (-e / 8.0f) + 0.16f * std::exp (-nearGrille / 7.0f));
                    r = g = b = 0.007f + 0.016f * rub * (0.5f + 0.5f * smoothstep (0.0f, 1.0f, std::abs (pebble.at (x, y))));
                    gloss = 0.11f - 0.04f * rub;
                    power = 11.0f;

                    const float scratch = juce::jmin (1.0f, fine.at (x, y) * 0.8f);
                    r += scratch * 0.02f; g += scratch * 0.02f; b += scratch * 0.02f;

                    const float tear = juce::jmax (deep.at (x, y) * 0.35f,
                                                   smoothstep (0.80f, 0.83f, wearNoise.fractal (lx * 0.11f, ly * 0.11f) + 0.10f * std::exp (-e / 9.0f)));
                    r += (0.17f - r) * tear;
                    g += (0.14f - g) * tear;
                    b += (0.10f - b) * tear;
                    gloss *= 1.0f - tear;

                    // A dark seam where the covering meets the grille and control strip.
                    const float seam = juce::jmin (std::abs (boxDistance (lx, ly, grilleArea.getUnion (stripArea), 1.0f)), 8.0f);
                    occlude *= 1.0f - 0.55f * std::exp (-seam / 2.5f);
                }

                const float shade = (1.0f - 0.72f * cast.at (x, y)) * (1.0f - 0.55f * contact.at (x, y)) * occlude;
                const float diffuse = 0.13f + 2.3f * att * ndl;
                const float spec = gloss * std::pow (ndh, power) * att * 1.6f;

                float or_ = (r * diffuse + spec) * shade + emitR * juce::jmin (1.0f, shade + 0.5f);
                float og = (g * diffuse + spec) * shade + emitG * juce::jmin (1.0f, shade + 0.5f);
                float ob = (b * diffuse + spec) * shade + emitB * juce::jmin (1.0f, shade + 0.5f);

                // Dark floor below the amp.
                const float floor = (0.008f + 0.012f * stainNoise.fractal (lx * 0.02f, ly * 0.4f)) * (0.3f + 1.6f * att);
                const float g0 = 1.0f + (grain.nextFloat() - 0.5f) * 0.02f;

                const auto enc = [&] (float linear, float base)
                {
                    const float mixed = (linear * inside + base * (1.0f - inside)) * g0;
                    return (juce::uint8) juce::roundToInt (255.0f * std::sqrt (juce::jlimit (0.0f, 1.0f, mixed)));
                };
                auto* p = (juce::PixelRGB*) data.getPixelPointer (x, y);
                p->setARGB (255, enc (or_, floor * 1.05f), enc (og, floor), enc (ob, floor * 0.95f));
            }
        }
    }

    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (s));

    drawJack (g, 78.0f, 325.0f);
    drawJack (g, 124.0f, 325.0f);

    // Slim anodised utility plate under the amp, for the plugin-only controls.
    const auto u = l.utility.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff202124), u.getX(), u.getY(),
                                             juce::Colour (0xff0b0b0c), u.getRight(), u.getBottom(), false));
    g.fillRoundedRectangle (u, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawHorizontalLine ((int) u.getY(), u.getX() + 3.0f, u.getRight() - 3.0f);

    return image;
}
} // namespace face
