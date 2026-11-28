/*
  RS Roar Ripper - Feedback Suppressor
  Copyright (c) 2026 Roar Sound

  SPDX-License-Identifier: AGPL-3.0-only

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as published by
  the Free Software Foundation, version 3 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <https://www.gnu.org/licenses/>.

  This software uses the JUCE framework (https://juce.com), which is also
  licensed under the GNU Affero General Public License version 3.
*/
#include "PluginEditor.h"

#include <algorithm>
#include <vector>

#define RS_USE_LOGO 1
#if RS_USE_LOGO
#include "BinaryData.h"
#endif

#define RS_USE_KNOB_IMAGE 1
#if RS_USE_KNOB_IMAGE
#include "BinaryData.h"
#endif

#define RS_USE_SPEED_ICONS 1
#if RS_USE_SPEED_ICONS
#include "BinaryData.h"
#endif

namespace
{
    constexpr double fMin = 20.0, fMax = 20000.0;

    // RTA-Anzeigebereich
    constexpr double dbTop = 5.0, dbBottom = -75.0;

    // Design-Raster
    constexpr float DW = 1200.0f;   // Live / Einpfeifen: 19" / 3HE
    constexpr float DW_HALF = 680.0f;   // Standalone: kompaktes Half-Rack
    constexpr float DH = 332.0f;

    // Designbreite aus dem TATSAECHLICHEN Seitenverhaeltnis ableiten.
    // Lehnt ein Host die Groessenaenderung ab, bleibt das Layout konsistent.
    float designWidthFor(int w, int h) noexcept
    {
        const float aspect = (float)w / (float)juce::jmax(1, h);
        return (aspect < 2.8f) ? DW_HALF : DW;
    }

    float biquadDb(const juce::IIRCoefficients& c, double f, double fs)
    {
        if (f <= 0.0 || fs <= 0.0) return 0.0f;
        const double w = 2.0 * rsdsp::kPi * f / fs;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const std::complex<double> z2 = std::polar(1.0, -2.0 * w);
        const auto num = (double)c.coefficients[0] + (double)c.coefficients[1] * z1
            + (double)c.coefficients[2] * z2;
        const auto den = 1.0 + (double)c.coefficients[3] * z1 + (double)c.coefficients[4] * z2;
        return (float)(20.0 * std::log10(std::abs(num / den) + 1.0e-12));
    }

    // ---- Hintergrund per Flood-Fill entfernen und freistellen ------------
    // Nur zusammenhaengende Randflaechen werden entfernt; innere helle
    // Details (z. B. die Silber-Einsaetze am Knopf) bleiben erhalten.
    juce::Image keyOutBackground(juce::Image raw, int maxW, int tol)
    {
        if (raw.isNull()) return {};

        if (raw.getWidth() > maxW)
        {
            const float sc = (float)maxW / (float)raw.getWidth();
            juce::Image small(juce::Image::ARGB, maxW,
                juce::jmax(1, (int)(raw.getHeight() * sc)), true);
            juce::Graphics g(small);
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(raw, 0, 0, small.getWidth(), small.getHeight(),
                0, 0, raw.getWidth(), raw.getHeight());
            raw = small;
        }

        raw = raw.convertedToFormat(juce::Image::ARGB);
        const int W = raw.getWidth(), H = raw.getHeight();
        if (W < 8 || H < 8) return raw;

        juce::Image::BitmapData bd(raw, juce::Image::BitmapData::readWrite);

        int br = 0, bgc = 0, bb = 0;
        const juce::Point<int> corners[4] = { { 2, 2 },{ W - 3, 2 },{ 2, H - 3 },{ W - 3, H - 3 } };
        for (const auto& p : corners)
        {
            const auto c = bd.getPixelColour(p.x, p.y);
            br += (int)c.getRed(); bgc += (int)c.getGreen(); bb += (int)c.getBlue();
        }
        br /= 4; bgc /= 4; bb /= 4;

        const bool alreadyCut = bd.getPixelColour(2, 2).getAlpha() < 250;

        if (!alreadyCut)
        {
            auto isBg = [&](int x, int y)
                {
                    const auto c = bd.getPixelColour(x, y);
                    const int d = juce::jmax(std::abs((int)c.getRed() - br),
                        juce::jmax(std::abs((int)c.getGreen() - bgc),
                            std::abs((int)c.getBlue() - bb)));
                    return d <= tol;
                };

            std::vector<unsigned char> mask((size_t)(W * H), 0);
            std::vector<int> stack;
            stack.reserve((size_t)(W * H / 4));

            auto push = [&](int x, int y)
                {
                    if (x < 0 || y < 0 || x >= W || y >= H) return;
                    const size_t idx = (size_t)(y * W + x);
                    if (mask[idx]) return;
                    if (!isBg(x, y)) return;
                    mask[idx] = 1;
                    stack.push_back((int)idx);
                };

            for (int x = 0; x < W; ++x) { push(x, 0); push(x, H - 1); }
            for (int y = 0; y < H; ++y) { push(0, y); push(W - 1, y); }

            while (!stack.empty())
            {
                const int idx = stack.back(); stack.pop_back();
                const int x = idx % W, y = idx / W;
                push(x - 1, y); push(x + 1, y);
                push(x, y - 1); push(x, y + 1);
            }

            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    if (mask[(size_t)(y * W + x)])
                        bd.setPixelColour(x, y, juce::Colours::transparentBlack);
        }

        int minX = W, minY = H, maxX = -1, maxY = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (bd.getPixelColour(x, y).getAlpha() > 24)
                {
                    minX = juce::jmin(minX, x); maxX = juce::jmax(maxX, x);
                    minY = juce::jmin(minY, y); maxY = juce::jmax(maxY, y);
                }

        if (maxX < minX || maxY < minY) return raw;
        return raw.getClippedImage({ minX, minY, maxX - minX + 1, maxY - minY + 1 });
    }

#if RS_USE_LOGO
    const juce::Image& getLogoSource()
    {
        static const juce::Image img = []
            {
                juce::Image raw = juce::ImageFileFormat::loadFrom(BinaryData::rs_logo_gelb_A4_jpg,
                    (size_t)BinaryData::rs_logo_gelb_A4_jpgSize);
                if (raw.isNull()) return juce::Image();

                if (raw.getWidth() > 900)
                {
                    const float sc = 900.0f / (float)raw.getWidth();
                    juce::Image small(juce::Image::ARGB, 900,
                        juce::jmax(1, (int)(raw.getHeight() * sc)), true);
                    juce::Graphics g(small);
                    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                    g.drawImage(raw, 0, 0, small.getWidth(), small.getHeight(),
                        0, 0, raw.getWidth(), raw.getHeight());
                    raw = small;
                }

                raw = raw.convertedToFormat(juce::Image::ARGB);
                int minX = raw.getWidth(), minY = raw.getHeight(), maxX = -1, maxY = -1;
                {
                    juce::Image::BitmapData bd(raw, juce::Image::BitmapData::readOnly);
                    for (int y = 0; y < raw.getHeight(); ++y)
                        for (int x = 0; x < raw.getWidth(); ++x)
                        {
                            const auto p = bd.getPixelColour(x, y);
                            const int r = (int)p.getRed(), gg = (int)p.getGreen(), b = (int)p.getBlue();
                            const int mn = juce::jmin(r, juce::jmin(gg, b));
                            const int mx = juce::jmax(r, juce::jmax(gg, b));
                            if (mn < 225 || mx - mn > 25)
                            {
                                minX = juce::jmin(minX, x); maxX = juce::jmax(maxX, x);
                                minY = juce::jmin(minY, y); maxY = juce::jmax(maxY, y);
                            }
                        }
                }
                if (maxX < minX || maxY < minY) return raw;
                const int pad = 3;
                minX = juce::jmax(0, minX - pad);
                minY = juce::jmax(0, minY - pad);
                maxX = juce::jmin(raw.getWidth() - 1, maxX + pad);
                maxY = juce::jmin(raw.getHeight() - 1, maxY + pad);
                return raw.getClippedImage({ minX, minY, maxX - minX + 1, maxY - minY + 1 });
            }();
        return img;
    }
#endif

    juce::Image makeLogoWatermark(int boxW, int boxH, float opacity)
    {
        juce::Image scaled;
#if RS_USE_LOGO
        const auto& src = getLogoSource();
        if (!src.isNull() && boxW > 4 && boxH > 4)
        {
            const float sc = juce::jmin((float)boxW / (float)src.getWidth(),
                (float)boxH / (float)src.getHeight());
            const int w = juce::jmax(1, (int)(src.getWidth() * sc));
            const int h = juce::jmax(1, (int)(src.getHeight() * sc));
            scaled = juce::Image(juce::Image::ARGB, w, h, true);
            {
                juce::Graphics g(scaled);
                g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                g.drawImage(src, 0, 0, w, h, 0, 0, src.getWidth(), src.getHeight());
            }
            juce::Image::BitmapData bd(scaled, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto p = bd.getPixelColour(x, y);
                    const int r = (int)p.getRed(), gg = (int)p.getGreen(), b = (int)p.getBlue();
                    const int mn = juce::jmin(r, juce::jmin(gg, b));
                    const int mx = juce::jmax(r, juce::jmax(gg, b));
                    float a = 1.0f - juce::jlimit(0.0f, 1.0f, (mn - 200.0f) / 55.0f);
                    if (mx - mn > 40) a = 1.0f;
                    bd.setPixelColour(x, y, p.withAlpha((juce::uint8)(255.0f * a * opacity)));
                }
            return scaled;
        }
#endif
        const int w = juce::jmax(8, boxW), h = juce::jmax(8, boxH);
        scaled = juce::Image(juce::Image::ARGB, w, h, true);
        juce::Graphics g(scaled);
        const float d = (float)juce::jmin(w, h);
        juce::Point<float> c(w * 0.5f, h * 0.5f);
        const float R = d * 0.36f;
        g.setColour(rsui::ink.withAlpha(opacity));
        g.fillEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f);
        g.setFont(juce::FontOptions(d * 0.22f, juce::Font::bold));
        g.setColour(rsui::vuPaper.withAlpha(opacity));
        g.drawText("RS", juce::Rectangle<float>(c.x - R, c.y - R * 0.5f, R * 2.0f, R).toNearestInt(),
            juce::Justification::centred);
        return scaled;
    }

#if RS_USE_KNOB_IMAGE
    const juce::Image& getKnobImage()
    {
        static const juce::Image img = keyOutBackground(
            juce::ImageFileFormat::loadFrom(BinaryData::knob_png, (size_t)BinaryData::knob_pngSize),
            512, 34);
        return img;
    }
#endif

#if RS_USE_SPEED_ICONS
    const juce::Image& getSpeedIcon(int idx)
    {
        static const juce::Image turtle = keyOutBackground(
            juce::ImageFileFormat::loadFrom(BinaryData::turtle_png, (size_t)BinaryData::turtle_pngSize), 160, 40);
        static const juce::Image elephant = keyOutBackground(
            juce::ImageFileFormat::loadFrom(BinaryData::elephant_png, (size_t)BinaryData::elephant_pngSize), 160, 40);
        static const juce::Image lion = keyOutBackground(
            juce::ImageFileFormat::loadFrom(BinaryData::lion_png, (size_t)BinaryData::lion_pngSize), 160, 40);
        static const juce::Image cheetah = keyOutBackground(
            juce::ImageFileFormat::loadFrom(BinaryData::cheetah_png, (size_t)BinaryData::cheetah_pngSize), 160, 40);

        switch (idx)
        {
        case 0:  return turtle;
        case 1:  return elephant;
        case 2:  return lion;
        default: return cheetah;
        }
    }
#endif

    // ---- Gezeichnete Speed-Piktogramme (Fallback, 24 x 16 Normraum) ------
    void drawSpeedIcon(juce::Graphics& g, juce::Rectangle<float> box, int idx,
        juce::Colour col, float s)
    {
        juce::ignoreUnused(s);

        const float sc = juce::jmin(box.getWidth() / 24.0f, box.getHeight() / 16.0f);
        auto t = juce::AffineTransform::scale(sc)
            .translated(box.getX() + (box.getWidth() - 24.0f * sc) * 0.5f,
                box.getY() + (box.getHeight() - 16.0f * sc) * 0.5f);
        g.setColour(col);

        if (idx == 0) // Schildkroete
        {
            juce::Path shell;
            shell.addEllipse(4.0f, 4.0f, 14.0f, 9.0f);
            g.fillPath(shell, t);
            juce::Path head;
            head.addEllipse(17.5f, 6.0f, 4.5f, 3.6f);
            g.fillPath(head, t);
            juce::Path legs;
            legs.addRectangle(5.5f, 12.0f, 2.2f, 3.2f);
            legs.addRectangle(9.0f, 12.5f, 2.2f, 3.0f);
            legs.addRectangle(13.0f, 12.5f, 2.2f, 3.0f);
            legs.addRectangle(16.0f, 12.0f, 2.2f, 3.2f);
            g.fillPath(legs, t);
        }
        else if (idx == 1) // Elefant
        {
            juce::Path body;
            body.addEllipse(3.0f, 5.0f, 14.0f, 9.0f);
            g.fillPath(body, t);
            juce::Path head;
            head.addEllipse(14.5f, 3.5f, 7.5f, 7.5f);
            g.fillPath(head, t);
            juce::Path ear;
            ear.addEllipse(13.0f, 3.0f, 5.5f, 6.5f);
            g.fillPath(ear, t);
            juce::Path trunk;
            trunk.startNewSubPath(20.5f, 8.5f);
            trunk.quadraticTo(23.5f, 11.0f, 21.5f, 15.0f);
            g.strokePath(trunk, juce::PathStrokeType(1.8f), t);
            juce::Path legs;
            legs.addRectangle(4.5f, 13.0f, 2.6f, 3.0f);
            legs.addRectangle(8.5f, 13.0f, 2.6f, 3.0f);
            legs.addRectangle(12.5f, 13.0f, 2.6f, 3.0f);
            g.fillPath(legs, t);
        }
        else if (idx == 2) // Loewe
        {
            juce::Path mane;
            const float cx = 8.5f, cy = 8.0f, r1 = 6.4f, r2 = 4.9f;
            for (int i = 0; i < 14; ++i)
            {
                const float a = (float)i * 0.4488f;
                const float a2 = a + 0.2244f;
                const float rr = (i % 2 == 0) ? r1 : r2;
                const float x = cx + std::cos(a) * rr;
                const float y = cy + std::sin(a) * rr;
                if (i == 0) mane.startNewSubPath(x, y);
                else        mane.lineTo(x, y);
                mane.lineTo(cx + std::cos(a2) * r2, cy + std::sin(a2) * r2);
            }
            mane.closeSubPath();
            g.fillPath(mane, t);
            juce::Path body;
            body.addEllipse(13.0f, 8.5f, 8.0f, 5.0f);
            g.fillPath(body, t);
            juce::Path tail;
            tail.startNewSubPath(20.5f, 9.5f);
            tail.quadraticTo(23.5f, 7.0f, 22.0f, 4.5f);
            g.strokePath(tail, juce::PathStrokeType(1.1f), t);
            juce::Path legs;
            legs.addRectangle(14.0f, 12.5f, 1.8f, 3.0f);
            legs.addRectangle(18.5f, 12.5f, 1.8f, 3.0f);
            g.fillPath(legs, t);
        }
        else // Gepard
        {
            juce::Path body;
            body.addEllipse(4.0f, 6.5f, 14.0f, 4.6f);
            g.fillPath(body, t);
            juce::Path head;
            head.addEllipse(17.0f, 4.2f, 4.6f, 4.2f);
            g.fillPath(head, t);
            juce::Path legs;
            legs.startNewSubPath(6.5f, 10.5f);  legs.lineTo(3.5f, 15.5f);
            legs.startNewSubPath(9.0f, 10.8f);  legs.lineTo(7.5f, 15.5f);
            legs.startNewSubPath(14.0f, 10.8f); legs.lineTo(16.5f, 15.5f);
            legs.startNewSubPath(16.5f, 10.5f); legs.lineTo(20.0f, 15.0f);
            g.strokePath(legs, juce::PathStrokeType(1.3f), t);
            juce::Path tail;
            tail.startNewSubPath(4.5f, 7.6f);
            tail.quadraticTo(0.5f, 6.0f, 1.5f, 2.5f);
            g.strokePath(tail, juce::PathStrokeType(1.2f), t);
        }
    }

    // ---- Bugreport-Helfer ------------------------------------------------
    juce::String getVersionString()
    {
#ifdef JucePlugin_VersionString
        return juce::String(JucePlugin_VersionString);
#else
        return "1.4.9";
#endif
    }

    juce::File getBugLogFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("RoarSound").getChildFile("RS Roar Ripper").getChildFile("RoarRipper.log");
    }

    void appendBugLog(const juce::String& msg)
    {
        auto f = getBugLogFile();
        f.getParentDirectory().createDirectory();
        if (f.getSize() > 200000)
        {
            auto content = f.loadFileAsString();
            f.replaceWithText(content.substring(juce::jmax(0, content.length() - 100000)));
        }
        f.appendText(juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") + "  " + msg + "\n");
    }

    juce::String readBugLogTail(int maxChars)
    {
        auto f = getBugLogFile();
        if (!f.existsAsFile()) return "(kein Log vorhanden)";
        auto content = f.loadFileAsString();
        if (content.length() > maxChars)
            content = "...(aeltere Eintraege gekuerzt)\n" + content.substring(content.length() - maxChars);
        return content;
    }

    juce::String mailtoEncodeUtf8(const juce::String& s)
    {
        const std::string utf8 = s.toStdString();
        juce::String out;
        for (unsigned char c : utf8)
        {
            if (c == '\n')      out += "%0D%0A";
            else if (c == '\r') continue;
            else if (c == ' ')  out += "%20";
            else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '-' || c == '_' || c == '.' || c == '~')
                out += (char)c;
            else
                out += juce::String::formatted("%%%02X", (int)c);
        }
        return out;
    }
}
// ===========================================================================
// rsui-Helfer
// ===========================================================================
void rsui::engravedText(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r,
    juce::Font f, juce::Justification j)
{
    g.setFont(f);
    g.setColour(rsui::cream.withAlpha(0.30f));
    g.drawFittedText(t, r.translated(0.0f, 1.0f).toNearestInt(), j, 1, 1.0f);
    g.setColour(rsui::ink);
    g.drawFittedText(t, r.toNearestInt(), j, 1, 1.0f);
}

void rsui::drawScrew(juce::Graphics& g, juce::Point<float> c, float r, juce::Random& rnd)
{
    for (int i = 0; i < 6; ++i)
    {
        const float a = rnd.nextFloat() * 6.283f;
        const float d = r * (1.1f + rnd.nextFloat() * 1.5f);
        const float rr = r * (0.12f + rnd.nextFloat() * 0.24f);
        g.setColour(juce::Colour(0xff8a5a2b).withAlpha(0.10f + rnd.nextFloat() * 0.18f));
        g.fillEllipse(c.x + std::cos(a) * d - rr, c.y + std::sin(a) * d - rr, rr * 2.0f, rr * 2.0f);
    }

    juce::ColourGradient gr(rsui::panelHi.brighter(0.15f), c.x - r, c.y - r,
        rsui::panelLo.darker(0.35f), c.x + r, c.y + r, false);
    g.setGradientFill(gr);
    g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour(rsui::panelEdge.withAlpha(0.85f));
    g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);

    const float a = rnd.nextFloat() * 3.14159f;
    juce::Path slot;
    slot.addRoundedRectangle(-r * 0.72f, -r * 0.16f, r * 1.44f, r * 0.32f, r * 0.1f);
    g.setColour(rsui::ink.withAlpha(0.75f));
    g.fillPath(slot, juce::AffineTransform::rotation(a).translated(c));
    g.setColour(rsui::cream.withAlpha(0.25f));
    g.strokePath(slot, juce::PathStrokeType(0.6f),
        juce::AffineTransform::rotation(a).translated(c.x, c.y + 0.6f));
}

void rsui::drawWear(juce::Graphics& g, juce::Rectangle<float> r, juce::Random& rnd,
    int scratches, float strength)
{
    for (int i = 0; i < scratches; ++i)
    {
        const float x = r.getX() + rnd.nextFloat() * r.getWidth();
        const float y = r.getY() + rnd.nextFloat() * r.getHeight();
        const float len = 3.0f + rnd.nextFloat() * 54.0f;
        const float dy = (rnd.nextFloat() - 0.5f) * 4.5f;
        g.setColour((rnd.nextBool() ? rsui::cream : rsui::ink)
            .withAlpha((0.03f + rnd.nextFloat() * 0.10f) * strength));
        g.drawLine(x, y, x + len, y + dy, 0.5f + rnd.nextFloat() * 0.8f);
    }

    for (int i = 0; i < scratches / 18 + 5; ++i)
    {
        const float x = r.getX() + rnd.nextFloat() * r.getWidth();
        const float y = r.getY() + rnd.nextFloat() * r.getHeight();
        const float rad = 9.0f + rnd.nextFloat() * 46.0f;
        juce::ColourGradient gr(rsui::ink.withAlpha(0.06f * strength), x, y,
            rsui::ink.withAlpha(0.0f), x + rad, y, true);
        g.setGradientFill(gr);
        g.fillEllipse(x - rad, y - rad, rad * 2.0f, rad * 2.0f);
    }

    for (int i = 0; i < scratches / 24 + 3; ++i)
    {
        const float x = r.getX() + rnd.nextFloat() * r.getWidth();
        const float y = r.getY() + rnd.nextFloat() * r.getHeight();
        const float w = 12.0f + rnd.nextFloat() * 44.0f;
        const float h = 4.0f + rnd.nextFloat() * 10.0f;
        juce::ColourGradient gr(rsui::cream.withAlpha(0.11f * strength), x, y,
            rsui::cream.withAlpha(0.0f), x + w, y, true);
        g.setGradientFill(gr);
        g.fillEllipse(x - w * 0.5f, y - h * 0.5f, w, h);
    }
}

// ---------------------------------------------------------------------------
// Der schwarze Hochglanz-Drehknopf mit Fluegelzeiger
// ---------------------------------------------------------------------------
void rsui::drawRoarKnob(juce::Graphics& g, juce::Point<float> c, float R,
    float angle, float s, bool lit, juce::Random& rnd)
{
    if (R < 3.0f) return;

    g.setColour(juce::Colours::black.withAlpha(0.34f));
    g.fillEllipse(c.x - R * 0.98f + 1.4f * s, c.y - R * 0.94f + 2.4f * s, R * 1.96f, R * 1.96f);

#if RS_USE_KNOB_IMAGE
    const auto& photo = getKnobImage();
    if (!photo.isNull())
    {
        const float sc = (2.0f * R) / (float)photo.getWidth();
        const float w = (float)photo.getWidth() * sc;
        const float h = (float)photo.getHeight() * sc;

        auto t = juce::AffineTransform::scale(sc)
            .translated(-w * 0.5f, -h * 0.5f)
            .rotated(angle)
            .translated(c);

        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.setOpacity(lit ? 1.0f : 0.68f);
        g.drawImageTransformed(photo, t, false);
        g.setOpacity(1.0f);

        rsui::drawWear(g, juce::Rectangle<float>(c.x - R, c.y - R, R * 2.0f, R * 2.0f),
            rnd, 8, 0.45f);
        return;
    }
#endif

    // ---------------- Fallback: gezeichneter Knopf ----------------
    const float dim = lit ? 1.0f : 0.70f;

    juce::ColourGradient dome(juce::Colour(0xff626262).withMultipliedBrightness(dim),
        c.x - R * 0.38f, c.y - R * 0.42f,
        juce::Colour(0xff040404), c.x + R * 0.85f, c.y + R * 0.90f, true);
    dome.addColour(0.45, juce::Colour(0xff1c1c1c));
    g.setGradientFill(dome);
    g.fillEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f);

    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f);
        g.reduceClipRegion(clip);
        juce::ColourGradient gloss(juce::Colours::white.withAlpha(0.22f * dim),
            c.x - R * 0.45f, c.y - R * 0.55f,
            juce::Colours::white.withAlpha(0.0f),
            c.x + R * 0.10f, c.y + R * 0.10f, true);
        g.setGradientFill(gloss);
        g.fillEllipse(c.x - R * 0.95f, c.y - R * 0.98f, R * 1.25f, R * 1.0f);
    }

    g.setColour(juce::Colour(0xff8a8a8a).withAlpha(0.35f * dim));
    g.drawEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f, 0.9f * s);

    auto tf = juce::AffineTransform::rotation(angle).translated(c);

    juce::Path collar;
    collar.addEllipse(-R * 0.56f, -R * 0.56f, R * 1.12f, R * 1.12f);
    juce::ColourGradient cg(juce::Colour(0xff303030), -R * 0.5f, -R * 0.5f,
        juce::Colour(0xff000000), R * 0.5f, R * 0.5f, false);
    g.setGradientFill(cg);
    g.fillPath(collar, tf);
    g.setColour(juce::Colour(0xff9a9a9a).withAlpha(0.32f * dim));
    g.strokePath(collar, juce::PathStrokeType(0.9f * s), tf);

    juce::Path collar2;
    collar2.addEllipse(-R * 0.46f, -R * 0.46f, R * 0.92f, R * 0.92f);
    g.setColour(juce::Colour(0xff141414));
    g.fillPath(collar2, tf);
    g.setColour(juce::Colour(0xff7a7a7a).withAlpha(0.26f * dim));
    g.strokePath(collar2, juce::PathStrokeType(0.7f * s), tf);

    for (int side = -1; side <= 1; side += 2)
    {
        const float ix = (float)side * R * 0.34f;
        const float ir = R * 0.155f;
        juce::Path ins;
        ins.addEllipse(ix - ir, -ir, ir * 2.0f, ir * 2.0f);
        juce::ColourGradient ig(juce::Colour(0xffdcdcdc), ix - ir, -ir,
            juce::Colour(0xff585858), ix + ir, ir, false);
        g.setGradientFill(ig);
        g.fillPath(ins, tf);

        g.setColour(juce::Colour(0xff3a3a3a).withAlpha(0.75f));
        for (int i = 0; i < 10; ++i)
        {
            const float a = (float)i * 0.31416f;
            juce::Path ln;
            ln.startNewSubPath(ix + std::cos(a) * ir * 0.22f, std::sin(a) * ir * 0.22f);
            ln.lineTo(ix + std::cos(a) * ir * 0.95f, std::sin(a) * ir * 0.95f);
            g.strokePath(ln, juce::PathStrokeType(0.5f * s), tf);
        }
        g.setColour(juce::Colour(0xff1e1e1e));
        g.strokePath(ins, juce::PathStrokeType(0.8f * s), tf);
    }

    juce::Path wing;
    wing.startNewSubPath(-R * 0.100f, -R * 1.26f);
    wing.quadraticTo(0.0f, -R * 1.36f, R * 0.100f, -R * 1.26f);
    wing.lineTo(R * 0.195f, R * 1.02f);
    wing.quadraticTo(R * 0.205f, R * 1.18f, 0.0f, R * 1.18f);
    wing.quadraticTo(-R * 0.205f, R * 1.18f, -R * 0.195f, R * 1.02f);
    wing.closeSubPath();

    g.setColour(juce::Colours::black.withAlpha(0.38f));
    g.fillPath(wing, tf.translated(1.4f * s, 1.8f * s));

    juce::ColourGradient wg(juce::Colour(0xff585858).withMultipliedBrightness(dim), -R * 0.20f, 0.0f,
        juce::Colour(0xff000000), R * 0.20f, 0.0f, false);
    wg.addColour(0.40, juce::Colour(0xff242424));
    wg.addColour(0.62, juce::Colour(0xff0a0a0a));
    g.setGradientFill(wg);
    g.fillPath(wing, tf);

    g.setColour(juce::Colour(0xffb4b4b4).withAlpha(0.35f * dim));
    g.strokePath(wing, juce::PathStrokeType(0.7f * s), tf);

    juce::Path spec;
    spec.startNewSubPath(-R * 0.075f, -R * 1.16f);
    spec.lineTo(-R * 0.135f, R * 0.94f);
    g.setColour(juce::Colours::white.withAlpha(0.55f * dim));
    g.strokePath(spec, juce::PathStrokeType(1.3f * s), tf);

    juce::Path tip;
    tip.startNewSubPath(0.0f, -R * 1.30f);
    tip.lineTo(0.0f, -R * 0.72f);
    g.setColour(juce::Colours::white.withAlpha(0.45f * dim));
    g.strokePath(tip, juce::PathStrokeType(0.9f * s), tf);

    rsui::drawWear(g, juce::Rectangle<float>(c.x - R, c.y - R, R * 2.0f, R * 2.0f), rnd, 16, 1.0f);
}

// ===========================================================================
// LookAndFeel
// ===========================================================================
rsui::LookAndFeel::LookAndFeel()
{
    setColour(juce::TextButton::textColourOffId, rsui::ink);
    setColour(juce::TextButton::textColourOnId, rsui::ink);
    setColour(juce::Label::textColourId, rsui::ink);
    setColour(juce::Slider::textBoxTextColourId, rsui::cream);
    setColour(juce::Slider::textBoxBackgroundColourId, rsui::bakelite);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, rsui::jewelAmb.withAlpha(0.35f));
}

// Reset / Export / Import: gleiche Elfenbein-Optik wie die Kippschalter
void rsui::LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
    const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const float rad = juce::jmin(4.0f, r.getHeight() * 0.22f);
    const bool lit = b.getToggleState();

    g.setColour(rsui::ink.withAlpha(0.34f));
    g.fillRoundedRectangle(r.translated(1.0f, 1.6f), rad);

    auto top = over ? rsui::plasticHi.brighter(0.05f) : rsui::plasticHi;
    auto bot = rsui::plasticLo;
    if (down) { top = top.darker(0.10f); bot = bot.darker(0.12f); }
    if (lit) {
        top = rsui::mix(top, rsui::jewelAmb, 0.35f);
        bot = rsui::mix(bot, rsui::jewelAmb, 0.30f);
    }

    juce::ColourGradient gr(top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false);
    g.setGradientFill(gr);
    g.fillRoundedRectangle(r, rad);

    g.setColour(rsui::plasticShadow.withAlpha(0.90f));
    g.drawRoundedRectangle(r, rad, 1.0f);

    g.setColour(rsui::cream.withAlpha(down ? 0.16f : 0.42f));
    g.drawLine(r.getX() + rad, r.getY() + 1.1f, r.getRight() - rad, r.getY() + 1.1f, 0.8f);

    juce::Random rnd((int)(size_t)&b);
    rsui::drawWear(g, r, rnd, 14, 0.65f);
}

// ===========================================================================
// Icon-Knoepfe
// ===========================================================================
void IconMetalButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const float d = juce::jmin(r.getWidth(), r.getHeight());
    auto c = r.getCentre();
    const float rad = d * 0.5f;

    g.setColour(rsui::ink.withAlpha(0.32f));
    g.fillEllipse(c.x - rad + 1.2f, c.y - rad + 1.8f, d, d);

    auto base = down ? rsui::panelLo : (over ? rsui::panelMid.brighter(0.10f) : rsui::panelMid);
    juce::ColourGradient gr(base.brighter(0.25f), c.x - rad, c.y - rad,
        base.darker(0.30f), c.x + rad, c.y + rad, false);
    g.setGradientFill(gr);
    g.fillEllipse(c.x - rad, c.y - rad, d, d);
    g.setColour(rsui::panelEdge);
    g.drawEllipse(c.x - rad, c.y - rad, d, d, 1.1f);

    if (getToggleState())
    {
        g.setColour(litColour().withAlpha(0.30f));
        g.fillEllipse(c.x - rad + 2.0f, c.y - rad + 2.0f, d - 4.0f, d - 4.0f);
    }

    juce::Random rnd((int)(size_t)this);
    rsui::drawWear(g, juce::Rectangle<float>(c.x - rad, c.y - rad, d, d), rnd, 10, 0.9f);
    drawIcon(g, juce::Rectangle<float>(c.x - rad, c.y - rad, d, d).reduced(d * 0.16f));
}

void InfoButton::drawIcon(juce::Graphics& g, juce::Rectangle<float> a)
{
    g.setColour(rsui::cream);
    g.setFont(juce::FontOptions(a.getHeight() * 0.95f, juce::Font::bold));
    g.drawText("i", a.toNearestInt(), juce::Justification::centred);
}

void CoffeeButton::drawIcon(juce::Graphics& g, juce::Rectangle<float> a)
{
    const float sc = juce::jmin(a.getWidth(), a.getHeight()) / 24.0f;
    auto t = juce::AffineTransform::scale(sc)
        .translated(a.getX() + (a.getWidth() - 24.0f * sc) * 0.5f,
            a.getY() + (a.getHeight() - 24.0f * sc) * 0.5f);

    juce::Path cup;
    cup.startNewSubPath(5.0f, 8.0f); cup.lineTo(18.0f, 8.0f);
    cup.lineTo(16.5f, 20.0f);        cup.lineTo(6.5f, 20.0f);
    cup.closeSubPath();
    g.setColour(rsui::ink);
    g.fillPath(cup, t);

    juce::Path handle;
    handle.startNewSubPath(18.0f, 10.0f);
    handle.quadraticTo(22.5f, 10.5f, 22.0f, 14.0f);
    handle.quadraticTo(21.5f, 17.5f, 17.5f, 17.5f);
    g.strokePath(handle, juce::PathStrokeType(1.8f), t);

    juce::Path steam;
    steam.startNewSubPath(9.0f, 6.0f);  steam.quadraticTo(10.0f, 4.0f, 9.0f, 2.0f);
    steam.startNewSubPath(13.0f, 6.0f); steam.quadraticTo(14.0f, 4.0f, 13.0f, 2.0f);
    g.setColour(rsui::ink.withAlpha(0.65f));
    g.strokePath(steam, juce::PathStrokeType(1.2f), t);
}

void BugButton::drawIcon(juce::Graphics& g, juce::Rectangle<float> a)
{
    const float sc = juce::jmin(a.getWidth(), a.getHeight()) / 24.0f;
    auto t = juce::AffineTransform::scale(sc)
        .translated(a.getX() + (a.getWidth() - 24.0f * sc) * 0.5f,
            a.getY() + (a.getHeight() - 24.0f * sc) * 0.5f);

    juce::Path body;
    body.addEllipse(6.5f, 8.0f, 11.0f, 13.0f);
    body.addEllipse(9.0f, 3.5f, 6.0f, 5.5f);
    g.setColour(rsui::ink);
    g.fillPath(body, t);

    juce::Path mid;
    mid.startNewSubPath(12.0f, 8.5f); mid.lineTo(12.0f, 20.5f);
    g.setColour(rsui::panelMid);
    g.strokePath(mid, juce::PathStrokeType(1.0f), t);

    juce::Path legs;
    legs.startNewSubPath(6.5f, 11.0f);  legs.lineTo(2.0f, 9.0f);
    legs.startNewSubPath(6.0f, 14.5f);  legs.lineTo(1.5f, 14.5f);
    legs.startNewSubPath(6.5f, 18.0f);  legs.lineTo(2.5f, 21.0f);
    legs.startNewSubPath(17.5f, 11.0f); legs.lineTo(22.0f, 9.0f);
    legs.startNewSubPath(18.0f, 14.5f); legs.lineTo(22.5f, 14.5f);
    legs.startNewSubPath(17.5f, 18.0f); legs.lineTo(21.5f, 21.0f);
    legs.startNewSubPath(10.0f, 4.0f);  legs.lineTo(8.0f, 1.0f);
    legs.startNewSubPath(14.0f, 4.0f);  legs.lineTo(16.0f, 1.0f);
    g.setColour(rsui::ink);
    g.strokePath(legs, juce::PathStrokeType(1.4f), t);
}

void UpdateButton::drawIcon(juce::Graphics& g, juce::Rectangle<float> a)
{
    const float sc = juce::jmin(a.getWidth(), a.getHeight()) / 24.0f;
    auto t = juce::AffineTransform::scale(sc)
        .translated(a.getX() + (a.getWidth() - 24.0f * sc) * 0.5f,
            a.getY() + (a.getHeight() - 24.0f * sc) * 0.5f);

    // Pfeil nach unten auf einer Grundlinie (Download)
    juce::Path arrow;
    arrow.startNewSubPath(12.0f, 3.0f);  arrow.lineTo(12.0f, 15.5f);
    arrow.startNewSubPath(6.0f, 10.0f);  arrow.lineTo(12.0f, 16.5f);  arrow.lineTo(18.0f, 10.0f);
    arrow.startNewSubPath(5.0f, 21.0f);  arrow.lineTo(19.0f, 21.0f);
    g.setColour(rsui::ink);
    g.strokePath(arrow, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded), t);
}

void FlagButton::drawIcon(juce::Graphics& g, juce::Rectangle<float> a)
{
    if (!english)
    {
        const float h3 = a.getHeight() / 3.0f;
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRect(a.getX(), a.getY(), a.getWidth(), h3);
        g.setColour(juce::Colour(0xffbb2222));
        g.fillRect(a.getX(), a.getY() + h3, a.getWidth(), h3);
        g.setColour(juce::Colour(0xffddaa22));
        g.fillRect(a.getX(), a.getY() + 2.0f * h3, a.getWidth(), a.getHeight() - 2.0f * h3);
    }
    else
    {
        const juce::Colour navy{ 0xff1b2a55 }, red{ 0xffa52a34 }, white{ 0xffe8e4d8 };
        g.setColour(navy);
        g.fillRect(a);

        juce::Path d1, d2;
        d1.startNewSubPath(a.getX(), a.getY());     d1.lineTo(a.getRight(), a.getBottom());
        d2.startNewSubPath(a.getRight(), a.getY()); d2.lineTo(a.getX(), a.getBottom());
        g.setColour(white);
        g.strokePath(d1, juce::PathStrokeType(a.getHeight() * 0.22f));
        g.strokePath(d2, juce::PathStrokeType(a.getHeight() * 0.22f));
        g.setColour(red);
        g.strokePath(d1, juce::PathStrokeType(a.getHeight() * 0.10f));
        g.strokePath(d2, juce::PathStrokeType(a.getHeight() * 0.10f));

        g.setColour(white);
        g.fillRect(a.getX(), a.getCentreY() - a.getHeight() * 0.16f, a.getWidth(), a.getHeight() * 0.32f);
        g.fillRect(a.getCentreX() - a.getWidth() * 0.16f, a.getY(), a.getWidth() * 0.32f, a.getHeight());
        g.setColour(red);
        g.fillRect(a.getX(), a.getCentreY() - a.getHeight() * 0.09f, a.getWidth(), a.getHeight() * 0.18f);
        g.fillRect(a.getCentreX() - a.getWidth() * 0.09f, a.getY(), a.getWidth() * 0.18f, a.getHeight());
    }

    g.setColour(rsui::panelEdge.withAlpha(0.7f));
    g.drawRect(a, 0.8f);
}

// ===========================================================================
// Wippschalter (Messung / Bypass / Mute)
// ===========================================================================
ToggleSwitch::ToggleSwitch(const juce::String& caption_)
    : juce::Button(caption_), caption(caption_)
{
    setClickingTogglesState(true);
}

void ToggleSwitch::buttonStateChanged()
{
    target = getToggleState() ? 1.0f : 0.0f;
    startTimerHz(60);
}

void ToggleSwitch::timerCallback()
{
    const float diff = target - pos;
    if (std::abs(diff) < 0.004f) { pos = target; stopTimer(); }
    else                          pos += diff * 0.35f;
    repaint();
}

void ToggleSwitch::paintButton(juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    const float s = getWidth() / 52.0f;
    const bool on = getToggleState();

    auto labelArea = r.removeFromTop(13.0f * s);
    const float capFont = (caption.length() > 7 ? 7.0f : 9.5f) * s;
    rsui::engravedText(g, caption, labelArea, juce::FontOptions(capFont, juce::Font::bold),
        juce::Justification::centred);

    const float lr = 3.0f * s;
    juce::Point<float> lc(r.getCentreX(), r.getY() + 5.0f * s);
    g.setColour(rsui::panelEdge);
    g.fillEllipse(lc.x - lr - 1.2f * s, lc.y - lr - 1.2f * s,
        (lr + 1.2f * s) * 2.0f, (lr + 1.2f * s) * 2.0f);
    if (on)
    {
        juce::ColourGradient glow(rsui::jewelAmb.withAlpha(0.50f), lc.x, lc.y,
            rsui::jewelAmb.withAlpha(0.0f), lc.x + lr * 3.2f, lc.y, true);
        g.setGradientFill(glow);
        g.fillEllipse(lc.x - lr * 3.2f, lc.y - lr * 3.2f, lr * 6.4f, lr * 6.4f);
    }
    juce::ColourGradient jewel(on ? rsui::jewelAmb.brighter(0.4f)
        : rsui::mix(rsui::panelLo, rsui::ink, 0.4f),
        lc.x - lr, lc.y - lr,
        on ? rsui::jewelAmb.darker(0.4f) : rsui::ink,
        lc.x + lr, lc.y + lr, false);
    g.setGradientFill(jewel);
    g.fillEllipse(lc.x - lr, lc.y - lr, lr * 2.0f, lr * 2.0f);

    r.removeFromTop(11.0f * s);

    auto body = r.reduced(5.0f * s, 2.0f * s);
    g.setColour(rsui::ink.withAlpha(0.38f));
    g.fillRoundedRectangle(body.translated(1.2f * s, 1.8f * s), 3.0f * s);

    juce::ColourGradient frame(over ? rsui::plasticHi.brighter(0.05f) : rsui::plasticHi,
        body.getX(), body.getY(),
        rsui::plasticLo, body.getX(), body.getBottom(), false);
    g.setGradientFill(frame);
    g.fillRoundedRectangle(body, 3.0f * s);
    g.setColour(rsui::plasticShadow.withAlpha(0.90f));
    g.drawRoundedRectangle(body, 3.0f * s, 1.0f * s);

    auto cav = body.reduced(3.5f * s);
    g.setColour(rsui::plasticShadow.darker(0.35f));
    g.fillRoundedRectangle(cav, 2.0f * s);

    const float tilt = pos * 2.0f - 1.0f;
    auto pad = cav.reduced(1.2f * s);
    const float ins = 2.2f * s;
    const float topInset = (tilt > 0.0f) ? ins * tilt : 0.0f;
    const float botInset = (tilt < 0.0f) ? ins * -tilt : 0.0f;

    juce::Path rock;
    rock.startNewSubPath(pad.getX() + topInset, pad.getY());
    rock.lineTo(pad.getRight() - topInset, pad.getY());
    rock.lineTo(pad.getRight() - botInset, pad.getBottom());
    rock.lineTo(pad.getX() + botInset, pad.getBottom());
    rock.closeSubPath();

    const float lit = (1.0f - tilt) * 0.5f;
    const auto cTop = rsui::plasticLo.interpolatedWith(rsui::plasticHi.brighter(0.05f), lit);
    const auto cBot = rsui::plasticHi.brighter(0.05f).interpolatedWith(rsui::plasticLo, lit);
    juce::ColourGradient rg(cTop, pad.getX(), pad.getY(), cBot, pad.getX(), pad.getBottom(), false);
    g.setGradientFill(rg);
    g.fillPath(rock);

    auto mid = pad.withSizeKeepingCentre(pad.getWidth() * 0.92f, pad.getHeight() * 0.34f);
    juce::ColourGradient dip(rsui::plasticShadow.withAlpha(0.22f), mid.getX(), mid.getCentreY(),
        rsui::plasticShadow.withAlpha(0.0f), mid.getX(), mid.getBottom(), false);
    g.setGradientFill(dip);
    g.fillEllipse(mid);

    g.setColour(rsui::plasticShadow.withAlpha(0.80f));
    g.strokePath(rock, juce::PathStrokeType(0.9f * s));
    g.setColour(rsui::cream.withAlpha(0.28f));
    g.drawLine(pad.getX() + topInset + 1.5f * s, pad.getY() + 1.1f * s,
        pad.getRight() - topInset - 1.5f * s, pad.getY() + 1.1f * s, 0.7f * s);

    g.setFont(juce::FontOptions(7.0f * s, juce::Font::bold));
    const auto ioTop = juce::Rectangle<float>(pad.getX(), pad.getY() + 3.0f * s, pad.getWidth(), 8.0f * s);
    const auto ioBot = juce::Rectangle<float>(pad.getX(), pad.getBottom() - 11.0f * s, pad.getWidth(), 8.0f * s);
    g.setColour(rsui::cream.withAlpha(0.30f));
    g.drawText("I", ioTop.translated(0.0f, 0.8f * s).toNearestInt(), juce::Justification::centred);
    g.drawText("O", ioBot.translated(0.0f, 0.8f * s).toNearestInt(), juce::Justification::centred);
    g.setColour(rsui::ink.withAlpha(0.55f));
    g.drawText("I", ioTop.toNearestInt(), juce::Justification::centred);
    g.drawText("O", ioBot.toNearestInt(), juce::Justification::centred);

    juce::Random rnd((int)(size_t)this);
    rsui::drawWear(g, body, rnd, 14, 0.7f);
}
// ===========================================================================
// RetroKnob  (optional mit eingebauter Ein/Aus-Funktion fuer HPF/LPF)
// ===========================================================================
RetroKnob::RetroKnob()
    : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
{
    setRotaryParameters(juce::degreesToRadians(-140.0f), juce::degreesToRadians(140.0f), true);
    setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 15);
    setColour(juce::Slider::textBoxTextColourId, rsui::cream);
    setColour(juce::Slider::textBoxBackgroundColourId, rsui::bakelite);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setMouseDragSensitivity(240);
}

void RetroKnob::setPowerParameter(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    powerParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter(id));
    repaint();
}

bool RetroKnob::isPowerOn() const noexcept
{
    return powerParam == nullptr ? true : powerParam->get();
}

void RetroKnob::mouseDown(const juce::MouseEvent& e)
{
    moved = false;
    juce::Slider::mouseDown(e);
}

void RetroKnob::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 3.0f)
        moved = true;
    juce::Slider::mouseDrag(e);
}

void RetroKnob::mouseUp(const juce::MouseEvent& e)
{
    juce::Slider::mouseUp(e);

    // Kurzer Klick ohne Ziehen = ein/aus
    if (!moved && powerParam != nullptr && !e.mods.isPopupMenu())
    {
        *powerParam = !powerParam->get();
        repaint();
    }
}

void RetroKnob::paint(juce::Graphics& g)
{
    const float s = (float)getWidth() / 96.0f;
    auto full = getLocalBounds().toFloat();

    float tbH = 0.0f;
    if (getTextBoxPosition() == juce::Slider::TextBoxBelow)
        tbH = (float)getTextBoxHeight();

    if (tbH > 0.0f)
    {
        const float tbW = (float)getTextBoxWidth();
        const juce::Rectangle<float> tb(full.getCentreX() - tbW * 0.5f,
            full.getBottom() - tbH, tbW, tbH);
        const auto plate = tb.expanded(1.5f * s, 1.0f * s);
        g.setColour(rsui::ink.withAlpha(0.55f));
        g.fillRoundedRectangle(plate, 2.0f * s);
        g.setColour(rsui::cream.withAlpha(0.18f));
        g.drawRoundedRectangle(plate, 2.0f * s, 0.8f);
    }

    auto area = full.withTrimmedBottom(tbH + 3.0f * s);
    const auto c = area.getCentre();
    const float R = juce::jmin(area.getWidth(), area.getHeight()) * 0.32f;
    if (R < 4.0f) return;

    const double mn = getMinimum(), mx = getMaximum();
    const float prop = (mx > mn) ? (float)((getValue() - mn) / (mx - mn)) : 0.0f;
    const auto rp = getRotaryParameters();
    const float ang = rp.startAngleRadians + prop * (rp.endAngleRadians - rp.startAngleRadians);

    // Skala auf dem Panel
    for (int i = 0; i <= 10; ++i)
    {
        const float a = rp.startAngleRadians
            + (rp.endAngleRadians - rp.startAngleRadians) * (float)i / 10.0f;
        const float sn = std::sin(a), cs = -std::cos(a);
        const float r1 = R * 1.50f, r2 = R * (i % 5 == 0 ? 1.62f : 1.56f);
        g.setColour(rsui::cream.withAlpha(0.22f));
        g.drawLine(c.x + sn * r1, c.y + cs * r1 + 0.8f * s,
            c.x + sn * r2, c.y + cs * r2 + 0.8f * s, 1.0f * s);
        g.setColour(rsui::ink.withAlpha(0.75f));
        g.drawLine(c.x + sn * r1, c.y + cs * r1,
            c.x + sn * r2, c.y + cs * r2, 1.0f * s);
    }

    juce::Random rnd((int)(size_t)this);
    rsui::drawRoarKnob(g, c, R, ang, s, isPowerOn(), rnd);

    // Eingebautes Laempchen (nur HPF/LPF)
    if (hasPower())
    {
        const bool on = isPowerOn();
        juce::Point<float> lc(area.getX() + 9.0f * s, area.getY() + 9.0f * s);
        const float lr = 3.4f * s;

        g.setColour(rsui::panelEdge);
        g.fillEllipse(lc.x - lr - 1.3f * s, lc.y - lr - 1.3f * s,
            (lr + 1.3f * s) * 2.0f, (lr + 1.3f * s) * 2.0f);
        if (on)
        {
            juce::ColourGradient glow(rsui::jewelGrn.withAlpha(0.50f), lc.x, lc.y,
                rsui::jewelGrn.withAlpha(0.0f), lc.x + lr * 3.2f, lc.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(lc.x - lr * 3.2f, lc.y - lr * 3.2f, lr * 6.4f, lr * 6.4f);
        }
        juce::ColourGradient jw(on ? rsui::jewelGrn.brighter(0.45f)
            : rsui::mix(rsui::panelLo, rsui::ink, 0.45f),
            lc.x - lr, lc.y - lr,
            on ? rsui::jewelGrn.darker(0.45f) : rsui::ink,
            lc.x + lr, lc.y + lr, false);
        g.setGradientFill(jw);
        g.fillEllipse(lc.x - lr, lc.y - lr, lr * 2.0f, lr * 2.0f);
        g.setColour(rsui::cream.withAlpha(on ? 0.55f : 0.18f));
        g.fillEllipse(lc.x - lr * 0.5f, lc.y - lr * 0.7f, lr * 0.7f, lr * 0.5f);
    }
}

// ===========================================================================
// ModeSelector  (groessere Beschriftung, Box wird im Rahmen gehalten)
// ===========================================================================
ModeSelector::ModeSelector()
    : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setRange(0.0, 2.0, 1.0);
    setRotaryParameters(juce::degreesToRadians(-55.0f), juce::degreesToRadians(55.0f), true);
    setMouseDragSensitivity(200);
    setScrollWheelEnabled(true);
    labels[0] = "LIVE";
    labels[1] = "STATISCH";
    labels[2] = "STANDALONE";
}

void ModeSelector::setLabels(const juce::String& a, const juce::String& b, const juce::String& c)
{
    labels[0] = a; labels[1] = b; labels[2] = c;
    repaint();
}

float ModeSelector::posAngle(int i)
{
    return juce::degreesToRadians(-55.0f + 55.0f * (float)i);
}

juce::Point<float> ModeSelector::knobCentre() const
{
    return { (float)getWidth() * 0.5f, (float)getHeight() * 0.62f };
}

float ModeSelector::knobRadius() const
{
    return juce::jmin((float)getWidth() * 0.185f, (float)getHeight() * 0.25f);
}

void ModeSelector::mouseDown(const juce::MouseEvent& e)
{
    auto p = e.getPosition().toFloat();
    auto c = knobCentre();

    if (c.getDistanceFrom(p) > knobRadius() * 1.6f)
    {
        const float ang = std::atan2(p.x - c.x, c.y - p.y);
        int best = 0;
        float bd = 1.0e9f;
        for (int i = 0; i < 3; ++i)
        {
            const float d = std::abs(ang - posAngle(i));
            if (d < bd) { bd = d; best = i; }
        }
        setValue((double)best, juce::sendNotificationSync);
        return;
    }

    juce::Slider::mouseDown(e);
}

void ModeSelector::paint(juce::Graphics& g)
{
    const float s = (float)getWidth() / 164.0f;
    auto c = knobCentre();
    const float R = knobRadius();
    const int sel = juce::roundToInt(getValue());

    const float boxW = 74.0f * s;
    const float boxH = 14.0f * s;

    for (int i = 0; i < 3; ++i)
    {
        const float a = posAngle(i);
        const bool active = (i == sel);

        // Rastpunkt mit Luft zur Fluegelspitze (Spitze ~1.48 R)
        const float dr = R * 1.64f;
        juce::Point<float> dp(c.x + std::sin(a) * dr, c.y - std::cos(a) * dr);
        if (active)
        {
            juce::ColourGradient glow(rsui::jewelAmb.withAlpha(0.55f), dp.x, dp.y,
                rsui::jewelAmb.withAlpha(0.0f), dp.x + 7.0f * s, dp.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(dp.x - 7.0f * s, dp.y - 7.0f * s, 14.0f * s, 14.0f * s);
        }
        g.setColour(active ? rsui::jewelAmb.brighter(0.25f) : rsui::ink.withAlpha(0.6f));
        g.fillEllipse(dp.x - 2.2f * s, dp.y - 2.2f * s, 4.4f * s, 4.4f * s);

        // Beschriftung: gross, aber immer vollstaendig im Component
        const float lr = R * 2.15f;
        juce::Point<float> lp(c.x + std::sin(a) * lr, c.y - std::cos(a) * lr);

        const float bx = juce::jlimit(boxW * 0.5f, (float)getWidth() - boxW * 0.5f, lp.x);
        const float by = juce::jlimit(boxH * 0.5f, (float)getHeight() - boxH * 0.5f, lp.y);
        auto box = juce::Rectangle<float>(bx - boxW * 0.5f, by - boxH * 0.5f, boxW, boxH);

        g.setFont(juce::FontOptions(11.0f * s, juce::Font::bold));
        g.setColour(rsui::cream.withAlpha(0.28f));
        g.drawFittedText(labels[i], box.translated(0.0f, 0.9f).toNearestInt(),
            juce::Justification::centred, 1, 1.0f);
        g.setColour(active ? rsui::ink : rsui::ink.withAlpha(0.55f));
        g.drawFittedText(labels[i], box.toNearestInt(), juce::Justification::centred, 1, 1.0f);
    }

    juce::Random rnd(4242);
    rsui::drawRoarKnob(g, c, R, posAngle(sel), s, true, rnd);
}

// ===========================================================================
// SpeedSelector  (nur Piktogramme - der Name steht im speedLabel des Editors)
// ===========================================================================
SpeedSelector::SpeedSelector()
    : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setRange(0.0, 3.0, 1.0);
    setRotaryParameters(juce::degreesToRadians(-90.0f), juce::degreesToRadians(90.0f), true);
    setMouseDragSensitivity(220);
    setScrollWheelEnabled(true);
}

float SpeedSelector::posAngle(int i)
{
    return juce::degreesToRadians(-90.0f + 60.0f * (float)i);
}

juce::Point<float> SpeedSelector::knobCentre() const
{
    return { (float)getWidth() * 0.5f, (float)getHeight() * 0.64f };
}

float SpeedSelector::knobRadius() const
{
    return juce::jmin((float)getWidth() * 0.15f, (float)getHeight() * 0.24f);
}

void SpeedSelector::mouseDown(const juce::MouseEvent& e)
{
    auto p = e.getPosition().toFloat();
    auto c = knobCentre();

    if (c.getDistanceFrom(p) > knobRadius() * 1.6f)
    {
        const float ang = std::atan2(p.x - c.x, c.y - p.y);
        int best = 0;
        float bd = 1.0e9f;
        for (int i = 0; i < 4; ++i)
        {
            const float d = std::abs(ang - posAngle(i));
            if (d < bd) { bd = d; best = i; }
        }
        setValue((double)best, juce::sendNotificationSync);
        return;
    }

    juce::Slider::mouseDown(e);
}

void SpeedSelector::paint(juce::Graphics& g)
{
    const float s = (float)getWidth() / 172.0f;
    auto c = knobCentre();
    const float R = knobRadius();
    const int sel = juce::roundToInt(getValue());

    for (int i = 0; i < 4; ++i)
    {
        const float a = posAngle(i);
        const bool active = (i == sel);

        // Rastpunkt mit Luft zur Fluegelspitze
        const float dr = R * 1.64f;
        juce::Point<float> dp(c.x + std::sin(a) * dr, c.y - std::cos(a) * dr);
        if (active)
        {
            juce::ColourGradient glow(rsui::jewelAmb.withAlpha(0.55f), dp.x, dp.y,
                rsui::jewelAmb.withAlpha(0.0f), dp.x + 7.0f * s, dp.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(dp.x - 7.0f * s, dp.y - 7.0f * s, 14.0f * s, 14.0f * s);
        }
        g.setColour(active ? rsui::jewelAmb.brighter(0.25f) : rsui::ink.withAlpha(0.6f));
        g.fillEllipse(dp.x - 2.2f * s, dp.y - 2.2f * s, 4.4f * s, 4.4f * s);

        // Piktogramm aussen herum
        const float lr = R * 2.45f;
        juce::Point<float> lp(c.x + std::sin(a) * lr, c.y - std::cos(a) * lr);
        auto box = juce::Rectangle<float>(lp.x - 14.0f * s, lp.y - 9.0f * s,
            28.0f * s, 18.0f * s);

        const auto colActive = active ? rsui::ink : rsui::ink.withAlpha(0.50f);
        const auto colEmboss = rsui::cream.withAlpha(0.28f);
        bool drawn = false;

#if RS_USE_SPEED_ICONS
        const auto& icon = getSpeedIcon(i);
        if (!icon.isNull())
        {
            const float isc = juce::jmin(box.getWidth() / (float)icon.getWidth(),
                box.getHeight() / (float)icon.getHeight());
            const float iw = (float)icon.getWidth() * isc;
            const float ih = (float)icon.getHeight() * isc;

            auto t = juce::AffineTransform::scale(isc)
                .translated(box.getCentreX() - iw * 0.5f,
                    box.getCentreY() - ih * 0.5f);

            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.setColour(colEmboss);
            g.drawImageTransformed(icon, t.translated(0.0f, 0.9f * s), true);
            g.setColour(colActive);
            g.drawImageTransformed(icon, t, true);
            drawn = true;
        }
#endif

        if (!drawn)
        {
            drawSpeedIcon(g, box.translated(0.0f, 0.9f * s), i, colEmboss, s);
            drawSpeedIcon(g, box, i, colActive, s);
        }
    }

    juce::Random rnd(777);
    rsui::drawRoarKnob(g, c, R, posAngle(sel), s, true, rnd);
}
// ===========================================================================
// VU-Meter
// ===========================================================================
VuMeter::VuMeter(const juce::String& caption_) : caption(caption_) {}

float VuMeter::needleAngle(float vu) const
{
    // Bereich -24 dB bis +3 dB = 27 dB Spanne
    const float p = juce::jlimit(0.0f, 1.0f, (vu + 24.0f) / 27.0f);
    return juce::degreesToRadians(-48.0f + p * 96.0f);
}

void VuMeter::update(double dt)
{
    const float t = juce::jlimit(-28.0f, 6.0f, targetVu);
    currentVu += (float)((t - currentVu) * (1.0 - std::exp(-dt / 0.300)));
    if (currentVu > 0.0f) peakHold = 1.0f;
    else                  peakHold = juce::jmax(0.0f, peakHold - (float)dt * 1.6f);
}

void VuMeter::resized() { buildFace(); }

void VuMeter::buildFace()
{
    const int W = juce::jmax(1, getWidth());
    const int H = juce::jmax(1, getHeight());
    face = juce::Image(juce::Image::ARGB, W, H, true);
    juce::Graphics g(face);
    juce::Random rnd(caption.hashCode());

    const float s = (float)W / 188.0f;
    auto r = getLocalBounds().toFloat();

    juce::ColourGradient bez(rsui::panelHi, r.getX(), r.getY(),
        rsui::panelLo.darker(0.25f), r.getX(), r.getBottom(), false);
    g.setGradientFill(bez);
    g.fillRoundedRectangle(r, 4.0f * s);
    g.setColour(rsui::panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f * s, 1.4f);

    auto inner = r.reduced(7.0f * s);
    inner.removeFromBottom(13.0f * s);

    juce::ColourGradient paper(rsui::vuPaper.brighter(0.05f), inner.getX(), inner.getY(),
        rsui::vuPaper2, inner.getX(), inner.getBottom(), false);
    g.setGradientFill(paper);
    g.fillRect(inner);

    for (int i = 0; i < 34; ++i)
    {
        const float x = inner.getX() + rnd.nextFloat() * inner.getWidth();
        const float y = inner.getY() + rnd.nextFloat() * inner.getHeight();
        const float rad = (4.0f + rnd.nextFloat() * 28.0f) * s;
        juce::ColourGradient stain(juce::Colour(0xffb99a5e).withAlpha(0.05f + rnd.nextFloat() * 0.10f),
            x, y, juce::Colour(0xffb99a5e).withAlpha(0.0f), x + rad, y, true);
        g.setGradientFill(stain);
        g.fillEllipse(x - rad, y - rad, rad * 2.0f, rad * 2.0f);
    }

    {
        const int bw = (int)(inner.getWidth() * 0.85f);
        const int bh = (int)(inner.getHeight() * 0.80f);
        auto logo = makeLogoWatermark(bw, bh, 0.30f);
        if (!logo.isNull())
            g.drawImageAt(logo,
                (int)(inner.getCentreX() - logo.getWidth() * 0.5f),
                (int)(inner.getCentreY() - logo.getHeight() * 0.5f - 2.0f * s));
    }

    juce::Point<float> pivot(inner.getCentreX(), inner.getBottom() + 6.0f * s);
    const float R = inner.getHeight() * 0.86f;

    const float ticks[] = { -24, -20, -15, -10, -7, -5, -3, -1, 0, 1, 2, 3 };
    for (float v : ticks)
    {
        const float a = needleAngle(v);
        const float sn = std::sin(a), cs = -std::cos(a);
        const bool major = (v == -24 || v == -20 || v == -10 || v == -5 || v == -3 || v == 0 || v == 3);
        g.setColour(v >= 0.0f ? rsui::redZone : rsui::ink);
        g.drawLine(pivot.x + sn * R * (major ? 0.86f : 0.91f), pivot.y + cs * R * (major ? 0.86f : 0.91f),
            pivot.x + sn * R, pivot.y + cs * R, (major ? 1.6f : 1.0f) * s);
        if (major)
        {
            juce::Point<float> lp(pivot.x + sn * R * 0.74f, pivot.y + cs * R * 0.74f);
            auto box = juce::Rectangle<float>(lp.x - 13.0f * s, lp.y - 6.0f * s, 26.0f * s, 12.0f * s);
            g.setFont(juce::FontOptions(8.0f * s, juce::Font::bold));
            g.setColour(v >= 0.0f ? rsui::redZone : rsui::ink);
            g.drawText(v > 0 ? "+" + juce::String((int)v) : juce::String((int)v),
                box.toNearestInt(), juce::Justification::centred);
        }
    }

    juce::Path arc;
    arc.addCentredArc(pivot.x, pivot.y, R, R, 0.0f, needleAngle(-24.0f), needleAngle(0.0f), true);
    g.setColour(rsui::ink.withAlpha(0.85f));
    g.strokePath(arc, juce::PathStrokeType(1.3f * s));
    juce::Path redArc;
    redArc.addCentredArc(pivot.x, pivot.y, R, R, 0.0f, needleAngle(0.0f), needleAngle(3.0f), true);
    g.setColour(rsui::redZone);
    g.strokePath(redArc, juce::PathStrokeType(2.4f * s));

    g.setFont(juce::FontOptions(15.0f * s, juce::Font::bold));
    g.setColour(rsui::ink.withAlpha(0.85f));
    g.drawText("VU", juce::Rectangle<float>(inner.getX(), inner.getBottom() - 26.0f * s,
        inner.getWidth(), 16.0f * s).toNearestInt(),
        juce::Justification::centred);

    g.setFont(juce::FontOptions(6.5f * s));
    g.setColour(rsui::ink.withAlpha(0.55f));
    g.drawText("0 VU = -18 dBFS", inner.reduced(4.0f * s).removeFromBottom(9.0f * s).toNearestInt(),
        juce::Justification::centredRight);

    rsui::drawWear(g, inner, rnd, 32, 0.85f);
    rsui::drawWear(g, r, rnd, 28, 1.1f);
    rsui::drawScrew(g, { r.getX() + 5.0f * s, r.getY() + 5.0f * s }, 2.2f * s, rnd);
    rsui::drawScrew(g, { r.getRight() - 5.0f * s, r.getY() + 5.0f * s }, 2.2f * s, rnd);
    rsui::drawScrew(g, { r.getX() + 5.0f * s, r.getBottom() - 5.0f * s }, 2.2f * s, rnd);
    rsui::drawScrew(g, { r.getRight() - 5.0f * s, r.getBottom() - 5.0f * s }, 2.2f * s, rnd);

    rsui::engravedText(g, caption, r.removeFromBottom(13.0f * s).reduced(24.0f * s, 0.0f),
        juce::FontOptions(9.5f * s, juce::Font::bold), juce::Justification::centred);
}

void VuMeter::paint(juce::Graphics& g)
{
    if (face.isNull()) buildFace();
    g.drawImageAt(face, 0, 0);

    const float s = (float)getWidth() / 188.0f;
    auto r = getLocalBounds().toFloat();
    auto inner = r.reduced(7.0f * s);
    inner.removeFromBottom(13.0f * s);

    juce::Point<float> pivot(inner.getCentreX(), inner.getBottom() + 6.0f * s);
    const float R = inner.getHeight() * 0.86f;
    const float a = needleAngle(currentVu);
    const float sn = std::sin(a), cs = -std::cos(a);

    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(inner.toNearestInt());
        g.setColour(rsui::ink.withAlpha(0.22f));
        g.drawLine(pivot.x + sn * R * 0.98f + 1.4f * s, pivot.y + cs * R * 0.98f + 1.4f * s,
            pivot.x + 1.4f * s, pivot.y + 1.4f * s, 2.0f * s);
        g.setColour(rsui::ink);
        g.drawLine(pivot.x, pivot.y, pivot.x + sn * R * 0.98f, pivot.y + cs * R * 0.98f, 1.9f * s);
        g.setColour(rsui::chromeDk);
        g.fillEllipse(pivot.x - 5.0f * s, pivot.y - 5.0f * s, 10.0f * s, 10.0f * s);
        g.setColour(rsui::chrome);
        g.fillEllipse(pivot.x - 3.0f * s, pivot.y - 3.0f * s, 6.0f * s, 6.0f * s);

        juce::ColourGradient glare(rsui::cream.withAlpha(0.16f), inner.getX(), inner.getY(),
            rsui::cream.withAlpha(0.0f), inner.getX() + inner.getWidth() * 0.7f,
            inner.getY() + inner.getHeight(), false);
        g.setGradientFill(glare);
        g.fillRect(inner);
    }

    juce::Point<float> pc(r.getRight() - 15.0f * s, r.getY() + 12.0f * s);
    const float pr = 3.0f * s;
    g.setColour(rsui::panelEdge);
    g.fillEllipse(pc.x - pr - 1.0f, pc.y - pr - 1.0f, (pr + 1.0f) * 2.0f, (pr + 1.0f) * 2.0f);
    if (peakHold > 0.01f)
    {
        juce::ColourGradient glow(rsui::jewelRed.withAlpha(0.5f * peakHold), pc.x, pc.y,
            rsui::jewelRed.withAlpha(0.0f), pc.x + pr * 3.5f, pc.y, true);
        g.setGradientFill(glow);
        g.fillEllipse(pc.x - pr * 3.5f, pc.y - pr * 3.5f, pr * 7.0f, pr * 7.0f);
    }
    g.setColour(peakHold > 0.01f ? rsui::jewelRed : rsui::mix(rsui::panelLo, rsui::ink, 0.5f));
    g.fillEllipse(pc.x - pr, pc.y - pr, pr * 2.0f, pr * 2.0f);
}
// ===========================================================================
// Spectrum  (-75 dB .. +5 dB)
// ===========================================================================
float SpectrumView::xFor(double f, juce::Rectangle<float> a) const
{
    return a.getX() + (float)(std::log(f / fMin) / std::log(fMax / fMin) * a.getWidth());
}

float SpectrumView::yFor(double db, juce::Rectangle<float> a) const
{
    return a.getY() + (float)((dbTop - db) / (dbTop - dbBottom) * a.getHeight());
}

void SpectrumView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float s = juce::jmax(0.5f, r.getWidth() / 688.0f);
    juce::Random rnd(9091);

    juce::ColourGradient bez(rsui::panelHi, r.getX(), r.getY(),
        rsui::panelLo.darker(0.3f), r.getX(), r.getBottom(), false);
    g.setGradientFill(bez);
    g.fillRoundedRectangle(r, 4.0f * s);
    g.setColour(rsui::panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f * s, 1.4f);

    auto a = r.reduced(8.0f * s);
    g.setColour(rsui::glass);
    g.fillRect(a);

    // ---- Frequenzraster ----
    g.setFont(juce::FontOptions(8.5f * s));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xFor(f, a);
        g.setColour(rsui::phosphor.withAlpha(0.13f));
        g.drawVerticalLine((int)x, a.getY(), a.getBottom());
        g.setColour(rsui::phosphor.withAlpha(0.45f));
        g.drawText(f >= 1000.0 ? juce::String(f / 1000.0, 0) + "k" : juce::String((int)f),
            (int)x + 2, (int)a.getBottom() - 11, 34, 10, juce::Justification::left);
    }

    // ---- dB-Raster: +5, 0, -15, -30, -45, -60, -75 ----
    for (double db : { 5.0, 0.0, -15.0, -30.0, -45.0, -60.0, -75.0 })
    {
        const float y = yFor(db, a);
        g.setColour(rsui::phosphor.withAlpha(db == 0.0 ? 0.24f : 0.13f));
        g.drawHorizontalLine((int)y, a.getX(), a.getRight());
        g.setColour(rsui::phosphor.withAlpha(0.45f));
        g.drawText(db > 0.0 ? "+" + juce::String((int)db) : juce::String((int)db),
            (int)a.getX() + 2, (int)y + 1, 30, 10, juce::Justification::left);
    }

    // ---- Spektrum ----
    const auto& det = proc.getEngine().getDetector();
    const auto& spec = det.getSpectrum();
    const int bins = det.getNumSpectrumBins();
    const double binHz = det.getBinHz();

    pts.clear();
    float lastX = -10.0f;
    for (int k = 1; k < bins; ++k)
    {
        const double f = k * binHz;
        if (f < fMin || f > fMax) continue;
        const float x = xFor(f, a);
        if (x - lastX < 1.0f) continue;
        lastX = x;
        const float db = juce::jlimit((float)dbBottom, (float)dbTop,
            spec[(size_t)k].load(std::memory_order_relaxed));
        pts.push_back({ x, yFor(db, a) });
    }

    if (pts.size() > 2)
    {
        juce::Path p;
        p.startNewSubPath(pts[0]);
        for (size_t i = 1; i < pts.size(); ++i) p.lineTo(pts[i]);

        juce::Path fill = p;
        fill.lineTo(pts.back().x, a.getBottom());
        fill.lineTo(pts.front().x, a.getBottom());
        fill.closeSubPath();

        g.setColour(rsui::phosphor.withAlpha(0.14f));
        g.fillPath(fill);
        g.setColour(rsui::phosphor.withAlpha(0.20f));
        g.strokePath(p, juce::PathStrokeType(3.0f * s));
        g.setColour(rsui::phosphor.withAlpha(0.95f));
        g.strokePath(p, juce::PathStrokeType(1.2f * s));
    }

    // ---- Kombinierte Filterkurve ----
    const double fs = proc.getAnalysisSampleRate();
    const auto& pub = proc.getEngine().getBank().published();
    const auto hc = juce::IIRCoefficients::makeHighPass(fs, (double)proc.getHpfFreq());
    const auto lc = juce::IIRCoefficients::makeLowPass(fs, (double)proc.getLpfFreq());

    juce::Path resp;
    bool started = false;
    const int w = juce::jmax(1, (int)a.getWidth());
    for (int px = 0; px <= w; px += 2)
    {
        const double f = fMin * std::pow(fMax / fMin, (double)px / (double)w);
        double db = 0.0;

        for (const auto& n : pub)
        {
            if (!n.active.load(std::memory_order_relaxed)) continue;
            const double d = n.depthDb.load(std::memory_order_relaxed);
            if (d > -0.05) continue;
            db += rsdsp::bellMagnitudeDb(n.freq.load(std::memory_order_relaxed), d,
                n.widthOct.load(std::memory_order_relaxed), f, fs);
        }

        if (proc.isHpfEnabled()) db += 3.0 * biquadDb(hc, f, fs);
        if (proc.isLpfEnabled()) db += 2.0 * biquadDb(lc, f, fs);

        const float y = yFor(juce::jlimit(dbBottom, dbTop, db), a);
        const float x = a.getX() + (float)px;
        if (!started) { resp.startNewSubPath(x, y); started = true; }
        else { resp.lineTo(x, y); }
    }
    g.setColour(rsui::cream.withAlpha(0.80f));
    g.strokePath(resp, juce::PathStrokeType(1.3f * s));

    // ---- Notch-Marker: Balken fuer alle, Text nur fuer dynamische ----
    g.setFont(juce::FontOptions(8.0f * s, juce::Font::bold));
    std::vector<std::pair<float, juce::String>> fixedLabels;

    for (const auto& n : pub)
    {
        if (!n.active.load(std::memory_order_relaxed)) continue;
        const float d = n.depthDb.load(std::memory_order_relaxed);
        if (d > -0.5f) continue;
        const float f = n.freq.load(std::memory_order_relaxed);
        if (f < fMin || f > fMax) continue;

        const float x = xFor(f, a);
        const bool isFixed = n.fixed.load(std::memory_order_relaxed);
        const auto col = isFixed ? rsui::jewelAmb : rsui::phosphor;

        g.setColour(col.withAlpha(0.85f));
        g.fillRect(juce::Rectangle<float>(x - 1.0f, yFor(0.0, a), 2.0f,
            yFor(d, a) - yFor(0.0, a)));

        if (isFixed)
        {
            fixedLabels.emplace_back(x, juce::String(juce::roundToInt(f)) + " Hz");
        }
        else
        {
            g.setColour(col);
            g.drawText(juce::String(juce::roundToInt(f)),
                (int)x - 22, (int)yFor(d, a) - 11, 44, 10,
                juce::Justification::centred);
        }
    }

    // ---- Feste Frequenzen unten, bei Ueberlappung eine Zeile hoch ----
    std::sort(fixedLabels.begin(), fixedLabels.end(),
        [](const std::pair<float, juce::String>& p,
            const std::pair<float, juce::String>& q) { return p.first < q.first; });

    struct Placed { float x, halfW; int line; };
    std::vector<Placed> placed;
    const float lineH = 13.0f * s;
    const float bottomY = a.getBottom() - 14.0f * s;

    for (const auto& fl : fixedLabels)
    {
        const float x = fl.first;
        const auto& txt = fl.second;
        const float halfW = (float)txt.length() * (8.0f * s) * 0.32f + 4.0f * s;

        int line = 1;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            bool clash = false;
            for (const auto& p : placed)
                if (p.line == attempt && std::abs(p.x - x) < p.halfW + halfW)
                {
                    clash = true; break;
                }

            if (!clash) { line = attempt; break; }
        }
        placed.push_back({ x, halfW, line });

        const float y = bottomY - (float)line * lineH;
        const int   ix = (int)(x - halfW);
        const int   iw = (int)(halfW * 2.0f);
        const int   ih = (int)lineH;

        g.setColour(rsui::cream.withAlpha(0.30f));
        g.drawText(txt, ix, (int)y + 1, iw, ih, juce::Justification::centred);
        g.setColour(rsui::jewelAmb);
        g.drawText(txt, ix, (int)y, iw, ih, juce::Justification::centred);
    }

    // ---- Scanlines, Glasreflex, Rahmen, Gebrauchsspuren ----
    g.setColour(juce::Colours::black.withAlpha(0.10f));
    for (float y = a.getY(); y < a.getBottom(); y += 3.0f)
        g.drawHorizontalLine((int)y, a.getX(), a.getRight());

    juce::ColourGradient glare(rsui::cream.withAlpha(0.07f), a.getX(), a.getY(),
        rsui::cream.withAlpha(0.0f), a.getX() + a.getWidth() * 0.6f,
        a.getY() + a.getHeight(), false);
    g.setGradientFill(glare);
    g.fillRect(a);
    g.setColour(rsui::ink.withAlpha(0.8f));
    g.drawRect(a, 1.2f);

    rsui::drawWear(g, r, rnd, 36, 1.1f);
    rsui::drawScrew(g, { r.getX() + 4.5f * s, r.getY() + 4.5f * s }, 2.2f * s, rnd);
    rsui::drawScrew(g, { r.getRight() - 4.5f * s, r.getY() + 4.5f * s }, 2.2f * s, rnd);
    rsui::drawScrew(g, { r.getX() + 4.5f * s, r.getBottom() - 4.5f * s }, 2.2f * s, rnd);
    rsui::drawScrew(g, { r.getRight() - 4.5f * s, r.getBottom() - 4.5f * s }, 2.2f * s, rnd);
}
// ===========================================================================
// InfoPopup
// ===========================================================================
InfoPopup::InfoPopup()
{
    setInterceptsMouseClicks(false, false);
    setVisible(false);
}

void InfoPopup::present(const juce::String& text, juce::Point<int> mouseLocal,
    juce::Rectangle<int> bounds, float scale)
{
    lastScale = scale;
    const float fontSize = 21.0f * scale;
    const float maxW = juce::jmin(430.0f * scale, (float)bounds.getWidth() - 24.0f);
    const float px = padX * scale;
    const float py = padY * scale;

    juce::AttributedString as;
    as.setJustification(juce::Justification::topLeft);
    as.setWordWrap(juce::AttributedString::byWord);
    as.append(text, juce::Font(juce::FontOptions(fontSize)), rsui::ink);

    layout.createLayout(as, maxW);
    const int w = (int)std::ceil(layout.getWidth()) + (int)(px * 2.0f);
    const int h = (int)std::ceil(layout.getHeight()) + (int)(py * 2.0f);

    int x = mouseLocal.x + (int)(16.0f * scale);
    int y = mouseLocal.y + (int)(20.0f * scale);
    if (x + w > bounds.getRight() - 4) x = bounds.getRight() - 4 - w;
    if (y + h > bounds.getBottom() - 4) y = mouseLocal.y - h - (int)(12.0f * scale);
    x = juce::jmax(bounds.getX() + 4, x);
    y = juce::jmax(bounds.getY() + 4, y);

    setBounds(x, y, w, h);
    setVisible(true);
    toFront(false);
    repaint();
}

void InfoPopup::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    juce::ColourGradient gr(rsui::vuPaper, r.getX(), r.getY(),
        rsui::vuPaper2, r.getX(), r.getBottom(), false);
    g.setGradientFill(gr);
    g.fillRoundedRectangle(r, 5.0f);

    juce::Random rnd(31);
    rsui::drawWear(g, r, rnd, 12, 0.6f);

    g.setColour(rsui::panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.3f);

    layout.draw(g, r.reduced(padX * lastScale, padY * lastScale));
}

// ===========================================================================
// Bugreport-Overlay
// ===========================================================================
BugReportOverlay::BugReportOverlay(RoarRipperAudioProcessor& p) : proc(p)
{
    setInterceptsMouseClicks(true, true);

    titleLabel.setText("BUGREPORT - RS ROAR RIPPER", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, rsui::ink);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    infoBox.setReadOnly(true);
    infoBox.setMultiLine(true);
    infoBox.setScrollbarsShown(true);
    infoBox.setCaretVisible(false);
    infoBox.setFont(juce::FontOptions(11.0f));
    infoBox.setColour(juce::TextEditor::backgroundColourId, rsui::glass);
    infoBox.setColour(juce::TextEditor::textColourId, rsui::phosphor);
    infoBox.setColour(juce::TextEditor::outlineColourId, rsui::panelEdge);
    addAndMakeVisible(infoBox);

    commentBox.setMultiLine(true);
    commentBox.setReturnKeyStartsNewLine(true);
    commentBox.setScrollbarsShown(true);
    commentBox.setFont(juce::FontOptions(12.0f));
    commentBox.setTextToShowWhenEmpty("Beschreibe hier das Problem so genau wie moeglich...",
        rsui::cream.withAlpha(0.4f));
    commentBox.setColour(juce::TextEditor::backgroundColourId, rsui::glass);
    commentBox.setColour(juce::TextEditor::textColourId, rsui::cream);
    commentBox.setColour(juce::TextEditor::outlineColourId, rsui::panelEdge);
    commentBox.setColour(juce::TextEditor::focusedOutlineColourId, rsui::jewelAmb);
    addAndMakeVisible(commentBox);

    hintLabel.setText("Beim Senden oeffnet sich dein E-Mail-Programm. Der Report liegt zusaetzlich in der Zwischenablage.",
        juce::dontSendNotification);
    hintLabel.setFont(juce::FontOptions(10.5f));
    hintLabel.setColour(juce::Label::textColourId, rsui::ink.withAlpha(0.75f));
    hintLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(hintLabel);

    sendButton.onClick = [this] { doSend(); };
    cancelButton.onClick = [this] { setVisible(false); };
    addAndMakeVisible(sendButton);
    addAndMakeVisible(cancelButton);

    setVisible(false);
}

void BugReportOverlay::show()
{
    infoBox.setText(gatherSnapshot(), false);
    commentBox.clear();
    commentBox.setText(juce::String());
    setVisible(true);
    toFront(false);
    resized();
    commentBox.grabKeyboardFocus();
}

juce::Rectangle<int> BugReportOverlay::getPanelBounds() const
{
    const int w = juce::jmin(getWidth() - 24, 620);
    const int h = juce::jmin(getHeight() - 16, 300);
    return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
}

juce::String BugReportOverlay::gatherSnapshot() const
{
    juce::String s;
    s << "Version:      v" << getVersionString() << "\n";
    s << "Datum:        " << juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") << "\n";
    s << "OS:           " << juce::SystemStats::getOperatingSystemName() << "\n";

    juce::String wrapper = "Host";
#if JucePlugin_Build_VST3
    wrapper = "VST3";
#elif JucePlugin_Build_Standalone
    wrapper = "Standalone";
#elif JucePlugin_Build_AUv3
    wrapper = "AUv3";
#elif JucePlugin_Build_AudioUnit
    wrapper = "AudioUnit";
#elif JucePlugin_Build_AAX
    wrapper = "AAX";
#elif JucePlugin_Build_VST
    wrapper = "VST";
#endif

    s << "Wrapper:      " << wrapper << "\n";
    s << "Samplerate:   " << juce::String((int)proc.getAnalysisSampleRate()) << " Hz\n";
    s << "Blockgroesse: " << proc.getBlockSize() << " Samples\n";

    const auto& det = proc.getEngine().getDetector();
    s << "Erkennung:    FFT=" << det.getFftSize()
        << ", Bin=" << juce::String(det.getBinHz(), 2) << " Hz"
        << ", Hop=" << det.getHop() << "\n";

    const auto& st = proc.getEngine().getSettings();
    const juce::String modeStr = st.mode == rsdsp::FeedbackEngine::Mode::live ? "Live"
        : st.mode == rsdsp::FeedbackEngine::Mode::setup ? "Statisch"
        : "Standalone";
    s << "Modus:        " << modeStr << "\n";
    s << "Messung:      " << (proc.isMeasuring() ? "aktiv" : "aus") << "\n";
    s << "Settings:     Sens=" << juce::String((int)(st.sensitivity * 100.0)) << "%"
        << ", Hold=" << juce::String((int)(st.holdSeconds * 1000.0)) << "ms"
        << ", Release=" << juce::String(st.releaseSeconds, 2) << "s"
        << ", Speed-Div=" << st.hopDivisor << "\n";
    s << "HPF:          " << (proc.isHpfEnabled() ? "an " : "aus ")
        << juce::String((int)proc.getHpfFreq()) << " Hz\n";
    s << "LPF:          " << (proc.isLpfEnabled() ? "an " : "aus ")
        << juce::String((int)proc.getLpfFreq()) << " Hz\n";

    const auto& pub = proc.getEngine().getBank().published();
    int active = 0, fixed = 0;
    juce::String notchLines;
    for (const auto& n : pub)
    {
        if (!n.active.load()) continue;
        ++active;
        if (n.fixed.load()) ++fixed;
        notchLines << "   " << juce::String(n.freq.load(), 1) << " Hz, "
            << juce::String(n.depthDb.load(), 1) << " dB, "
            << (n.fixed.load() ? "fest" : "dynamisch") << "\n";
    }
    s << "Notches:      " << active << " aktiv (" << fixed << " fest)\n";
    if (active > 0) s << notchLines;
    return s;
}

juce::String BugReportOverlay::buildReportBody(const juce::String& comment) const
{
    juce::String b;
    b << "Bugreport RS Roar Ripper\n\nBeschreibung:\n";
    b << (comment.isNotEmpty() ? comment : juce::String("(kein Kommentar)")) << "\n\n";
    b << "--- Automatisch erfasste Daten ---\n" << gatherSnapshot();
    b << "\nLog-Auszug:\n" << readBugLogTail(600) << "\n";
    return b;
}

void BugReportOverlay::doSend()
{
    const juce::String comment = commentBox.getText().trim();
    juce::String body = buildReportBody(comment);
    if (body.length() > 1900)
        body = body.substring(0, 1900) + "\n...(gekuerzt)";

    const juce::String subject = juce::String("Bugreport fuer RS Roar Ripper v") + getVersionString();
    juce::SystemClipboard::copyTextToClipboard(juce::String("Betreff: ") + subject + "\n\n" + body);

    juce::String url("mailto:info@roarsound.de");
    url += "?subject=" + mailtoEncodeUtf8(subject);
    url += "&body=" + mailtoEncodeUtf8(body);
    juce::URL(url).launchInDefaultBrowser();

    appendBugLog("Bugreport gesendet");
    setVisible(false);
}

void BugReportOverlay::paint(juce::Graphics& g)
{
    g.fillAll(rsui::ink.withAlpha(0.72f));
    auto panelR = getPanelBounds().toFloat();

    juce::ColourGradient gr(rsui::panelHi, panelR.getX(), panelR.getY(),
        rsui::panelLo, panelR.getX(), panelR.getBottom(), false);
    g.setGradientFill(gr);
    g.fillRoundedRectangle(panelR, 6.0f);
    g.setColour(rsui::panelEdge);
    g.drawRoundedRectangle(panelR.reduced(0.5f), 6.0f, 1.6f);

    juce::Random rnd(5150);
    rsui::drawWear(g, panelR, rnd, 70, 1.1f);
    rsui::drawScrew(g, { panelR.getX() + 10.0f,     panelR.getY() + 10.0f }, 3.0f, rnd);
    rsui::drawScrew(g, { panelR.getRight() - 10.0f, panelR.getY() + 10.0f }, 3.0f, rnd);
    rsui::drawScrew(g, { panelR.getX() + 10.0f,     panelR.getBottom() - 10.0f }, 3.0f, rnd);
    rsui::drawScrew(g, { panelR.getRight() - 10.0f, panelR.getBottom() - 10.0f }, 3.0f, rnd);
}

void BugReportOverlay::resized()
{
    auto panelR = getPanelBounds().reduced(20, 14);

    titleLabel.setBounds(panelR.removeFromTop(22));
    panelR.removeFromTop(6);
    infoBox.setBounds(panelR.removeFromTop(juce::jmin(100, panelR.getHeight() / 2)));
    panelR.removeFromTop(6);

    auto buttons = panelR.removeFromBottom(26);
    panelR.removeFromBottom(4);
    auto hint = panelR.removeFromBottom(14);
    panelR.removeFromBottom(4);

    commentBox.setBounds(panelR);
    hintLabel.setBounds(hint);
    sendButton.setBounds(buttons.removeFromRight(100));
    buttons.removeFromRight(8);
    cancelButton.setBounds(buttons.removeFromRight(100));
}

void BugReportOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (!getPanelBounds().contains(e.getPosition()))
        setVisible(false);
}
// ===========================================================================
// Editor
// ===========================================================================
RoarRipperAudioProcessorEditor::RoarRipperAudioProcessorEditor(RoarRipperAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p), spectrum(p), bugOverlay(p),
    vuIn("INPUT"), vuOut("OUTPUT"),
    measureSwitch("MESSUNG"), bypassSwitch("BYPASS"), muteSwitch("MUTE")
{
    setLookAndFeel(&lnf);

    addAndMakeVisible(spectrum);
    addAndMakeVisible(vuIn);
    addAndMakeVisible(vuOut);

    versionLabel.setFont(juce::FontOptions(11.0f));
    versionLabel.setColour(juce::Label::textColourId, rsui::ink);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setText("v" + getVersionString(), juce::dontSendNotification);
    addAndMakeVisible(versionLabel);

    cpuLabel.setFont(juce::FontOptions(11.0f));
    cpuLabel.setColour(juce::Label::textColourId, rsui::ink);
    cpuLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(cpuLabel);

    // Beschriftung des Speed-Waehlers als echtes Component
    speedLabel.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    speedLabel.setColour(juce::Label::textColourId, rsui::ink);
    speedLabel.setJustificationType(juce::Justification::centred);
    speedLabel.setText("GESCHWINDIGKEIT", juce::dontSendNotification);
    addAndMakeVisible(speedLabel);

    addAndMakeVisible(modeSelector);
    modeSelector.onValueChange = [this] { updateVisibility(); };

    addAndMakeVisible(speedSelector);

    measureSwitch.onClick = [this]
        {
            if (measureSwitch.getToggleState())
            {
                appendBugLog("Messung gestartet");
                proc.startMeasurement();
            }
            else
            {
                appendBugLog("Messung gestoppt, Notches eingefroren");
                proc.stopMeasurement();
            }
        };
    addAndMakeVisible(measureSwitch);
    addAndMakeVisible(bypassSwitch);
    addAndMakeVisible(muteSwitch);

    resetButton.onClick = [this] { appendBugLog("Reset"); proc.requestReset(); };
    addAndMakeVisible(resetButton);

    exportButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Preset exportieren",
                juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("RoarRipper.xml"),
                "*.xml");
            fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles,
                [pp = &proc](const juce::FileChooser& c)
                {
                    auto f = c.getResult();
                    if (f != juce::File()) { pp->exportToFile(f); appendBugLog("Preset exportiert"); }
                });
        };
    addAndMakeVisible(exportButton);

    importButton.onClick = [this]
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Preset importieren",
                juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.xml");
            fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
                [pp = &proc](const juce::FileChooser& c)
                {
                    auto f = c.getResult();
                    if (f != juce::File()) { pp->importFromFile(f); appendBugLog("Preset importiert"); }
                });
        };
    addAndMakeVisible(importButton);

    infoButton.setClickingTogglesState(true);
    infoButton.onClick = [this]
        {
            infoMode = infoButton.getToggleState();
            if (!infoMode) { infoPopup.setVisible(false); hoverSeconds = 0.0; }
        };
    addAndMakeVisible(infoButton);

    addAndMakeVisible(flagButton);
    flagButton.onClick = [this] { setLanguage(!english); };

    addAndMakeVisible(coffeeButton);
    coffeeButton.onClick = [this]
        {
            juce::URL("https://paypal.me/RoarSound").launchInDefaultBrowser();
            appendBugLog("Kaffee-Button");
        };

    addAndMakeVisible(bugButton);
    bugButton.onClick = [this] { appendBugLog("Bugreport geoeffnet"); bugOverlay.show(); };

    // Update-Knopf: bleibt unsichtbar, bis eine neuere Version auf GitHub gefunden wurde
    updateButton.onClick = [this]
        {
            auto url = rsupd::getInfo().url;
            if (url.isEmpty())
                url = rsupd::releasesPageUrl();

            appendBugLog("Update-Knopf");
            juce::URL(url).launchInDefaultBrowser();
        };
    addChildComponent(updateButton);

    addChildComponent(bugOverlay);
    addChildComponent(infoPopup);

    setupKnob(sens, "sens", "SENSITIVITY");
    setupKnob(hold, "hold", "HOLD");
    setupKnob(release, "release", "RELEASE");
    setupKnob(hpf, "hpfFreq", "HPF");
    setupKnob(lpf, "lpfFreq", "LPF");

    // Ein/Aus direkt im Regler (Klick = Laempchen)
    hpf.slider.setPowerParameter(proc.parameters, "hpfEnabled");
    lpf.slider.setPowerParameter(proc.parameters, "lpfEnabled");

    sens.slider.textFromValueFunction = [](double v)
        { return juce::String(juce::roundToInt(v * 100.0)) + " %"; };
    sens.slider.valueFromTextFunction = [](const juce::String& t)
        { return t.getDoubleValue() / 100.0; };

    hold.slider.textFromValueFunction = [](double v)
        { return juce::String(juce::roundToInt(v)) + " ms"; };
    hold.slider.valueFromTextFunction = [](const juce::String& t)
        { return t.getDoubleValue(); };

    release.slider.textFromValueFunction = [](double v)
        { return juce::String(v, 2) + " s"; };
    release.slider.valueFromTextFunction = [](const juce::String& t)
        { return t.getDoubleValue(); };

    auto hzText = [](double v)
        {
            return v >= 1000.0 ? juce::String(v / 1000.0, 1) + " kHz"
                : juce::String(juce::roundToInt(v)) + " Hz";
        };
    hpf.slider.textFromValueFunction = hzText;
    hpf.slider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    lpf.slider.textFromValueFunction = hzText;
    lpf.slider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };

    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.parameters, "mode", modeSelector);
    speedAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.parameters, "speed", speedSelector);
    abAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.parameters, "ab", bypassSwitch);
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.parameters, "mute", muteSwitch);

    infoTargets = { &modeSelector, &speedSelector, &speedLabel,
                    &measureSwitch, &bypassSwitch, &muteSwitch,
                    &sens.slider, &hold.slider, &release.slider, &hpf.slider, &lpf.slider,
                    &resetButton, &exportButton, &importButton,
                    &infoButton, &flagButton, &coffeeButton, &bugButton, &updateButton,
                    &vuIn, &vuOut, &spectrum, &cpuLabel, &versionLabel };

    setupInfoTexts();

    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double)DW / (double)DH);
    setResizeLimits(900, 249, 1920, 531);
    setSize((int)DW, (int)DH);

    updateVisibility();

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
    appendBugLog("Editor gestartet, Version v" + getVersionString());

#if RS_ENABLE_UPDATE_CHECK
    // Prueft im Hintergrund (eigener Thread) auf eine neuere Version; blockiert nie.
    updateChecker = std::make_unique<rsupd::UpdateChecker>(getVersionString());
#endif
}

RoarRipperAudioProcessorEditor::~RoarRipperAudioProcessorEditor()
{
    stopTimer();
    updateChecker.reset();
    setLookAndFeel(nullptr);
}

void RoarRipperAudioProcessorEditor::setupKnob(Knob& k, const juce::String& id, const juce::String& name)
{
    addAndMakeVisible(k.slider);
    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, rsui::ink);
    addAndMakeVisible(k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.parameters, id, k.slider);
}

void RoarRipperAudioProcessorEditor::setLanguage(bool en)
{
    english = en;
    flagButton.setEnglish(en);
    modeSelector.setLabels("LIVE", en ? "STATIC" : "STATISCH", "STANDALONE");
    measureSwitch.setCaption(en ? "MEASUREMENT" : "MESSUNG");
    speedLabel.setText(en ? "SPEED" : "GESCHWINDIGKEIT", juce::dontSendNotification);
    setupInfoTexts();
    infoPopup.setVisible(false);
    hoverSeconds = 0.0;
    repaint();
}

void RoarRipperAudioProcessorEditor::setupInfoTexts()
{
    if (!english)
    {
        infoButton.setTooltip("Info ein/aus: Erklaerungen anzeigen. Maus 1 Sekunde still ueber einem Bedienelement halten.");
        flagButton.setTooltip("Sprache: Zwischen Deutsch und Englisch umschalten.");
        coffeeButton.setTooltip("Auch wenn die KI 95 % der Arbeit uebernommen hat, stecken viele Stunden Arbeit in diesem Plug-in. "
            "Vielleicht ist es Dir einen Kaffee oder Big Mac wert, einen Tontechnikerkollegen zu unterstuetzen. "
            "Falls nicht, ist es aber auch in Ordnung. Geniesse in jedem Fall die Arbeit damit. :)");
        bugButton.setTooltip("Bugreport: Oeffnet ein Fenster mit Kommentarfeld und sendet einen Report an info@roarsound.de.");
        updateButton.setTooltip("Update verfuegbar" + (rsupd::getInfo().tag.isNotEmpty() ? ": " + rsupd::getInfo().tag : juce::String())
            + ". Klick oeffnet die Download-Seite auf GitHub.");

        vuIn.setTip("Eingangspegel (VU): Pegel vor der Bearbeitung. 0 VU entspricht -18 dBFS. "
            "Skala von -24 bis +3. Die Nadel folgt dem Signal mit etwa 300 ms Traegheit, "
            "wie bei einem echten VU-Meter.");
        vuOut.setTip("Ausgangspegel (VU): Pegel nach HPF/LPF und Notches. Skala von -24 bis +3. "
            "Das rote Laempchen leuchtet, sobald 0 VU ueberschritten wird.");
        spectrum.setTip("Spectrum: Analyse des Ausgangssignals, Anzeigebereich -75 bis +5 dB, "
            "mit der kombinierten Filterkurve. Bernsteinfarbene Marker = fest eingebrannte "
            "Notches (Frequenzen unten am Rand, bei Bedarf zweizeilig), "
            "gruene Marker = dynamische Notches.");

        modeSelector.setTooltip("Betriebsart: Live = automatische Notches. Statisch = Notches werden fest eingebrannt. "
            "Standalone = kompaktes Half-Rack ohne RTA und ohne HPF/LPF. "
            "Klick auf die Beschriftung oder drehen.");
        speedSelector.setTooltip("Geschwindigkeit: Analyserate der Erkennung. Von links nach rechts: "
            "Schildkroete (am sparsamsten), Elefant, Loewe, Gepard (am schnellsten, hoechste CPU). "
            "Der Fluegel zeigt auf die aktive Stufe.");
        speedLabel.setTooltip("Geschwindigkeit: Analyserate der Erkennung. Die vier Piktogramme stehen fuer "
            "Schildkroete, Elefant, Loewe und Gepard.");
        measureSwitch.setTooltip("Messung: Ein startet die Einmessung, das Laempchen leuchtet. "
            "Waehrend der Messung arbeiten nur die festen Notches, dynamische sind pausiert. "
            "Aus beendet die Messung und haelt alle erkannten Notches fest.");
        resetButton.setTooltip("Reset: Entfernt alle festen und dynamischen Notches. Im Standalone-Modus ausgeblendet.");
        bypassSwitch.setTooltip("Bypass: Signal unbearbeitet durchleiten. Alle Notches bleiben intern erhalten.");
        muteSwitch.setTooltip("Mute: Ausgang wird mit 5 ms Rampe stummgeschaltet.");
        exportButton.setTooltip("Export: Speichert Einstellungen und feste Notches als XML.");
        importButton.setTooltip("Import: Laedt ein XML-Preset inklusive fester Notches. Die bisherigen festen Notches werden ersetzt.");
        sens.slider.setTooltip("Sensitivity: Empfindlichkeit der Feedback-Erkennung. Ziehen zum Aendern. Standard 70 %.");
        hold.slider.setTooltip("Hold: Haltezeit nach dem letzten Feedback-Treffer. Ziehen zum Aendern.");
        release.slider.setTooltip("Release: Rueckfahrzeit des Notches auf 0 dB. Ziehen zum Aendern.");
        hpf.slider.setTooltip("HPF: Hochpass 36 dB/Oktave. Kurz klicken schaltet ein/aus (gruenes Laempchen). "
            "Mit gedrueckter Maustaste ziehen stellt die Grenzfrequenz ein.");
        lpf.slider.setTooltip("LPF: Tiefpass 24 dB/Oktave. Kurz klicken schaltet ein/aus (gruenes Laempchen). "
            "Mit gedrueckter Maustaste ziehen stellt die Grenzfrequenz ein.");
        cpuLabel.setInfoText("CPU: Rechenzeit dieses Plugins im Verhaeltnis zur Blockzeit.");
        versionLabel.setInfoText("Version: Build-Stand des Plugins.");
    }
    else
    {
        infoButton.setTooltip("Info on/off: show explanations. Hover one second over a control.");
        flagButton.setTooltip("Language: switch between German and English.");
        coffeeButton.setTooltip("Even though AI handled 95 % of the work, many hours of effort went into this plug-in. "
            "Maybe it's worth a coffee or a Big Mac to support a fellow audio engineer. "
            "If not, that's perfectly fine too. Enjoy working with it either way. :)");
        bugButton.setTooltip("Bug report: opens a comment window and sends a report to info@roarsound.de.");
        updateButton.setTooltip("Update available" + (rsupd::getInfo().tag.isNotEmpty() ? ": " + rsupd::getInfo().tag : juce::String())
            + ". Click to open the download page on GitHub.");

        vuIn.setTip("Input level (VU): level before processing. 0 VU equals -18 dBFS. "
            "Scale from -24 to +3. The needle follows the signal with about 300 ms ballistics, "
            "like a real VU meter.");
        vuOut.setTip("Output level (VU): level after HPF/LPF and notches. Scale from -24 to +3. "
            "The red jewel lights up as soon as 0 VU is exceeded.");
        spectrum.setTip("Spectrum: analysis of the output signal, display range -75 to +5 dB, "
            "plus the combined filter curve. Amber markers = burned-in notches "
            "(frequencies at the bottom edge, two rows if needed), "
            "green markers = dynamic notches.");

        modeSelector.setTooltip("Mode: Live = automatic notches. Static = notches are burned in permanently. "
            "Standalone = compact half-rack without RTA and without HPF/LPF. "
            "Click a label or drag.");
        speedSelector.setTooltip("Speed: analysis rate of detection. Left to right: "
            "Turtle (most efficient), Elephant, Lion, Cheetah (fastest, highest CPU). "
            "The wing points at the active step.");
        speedLabel.setTooltip("Speed: analysis rate of detection. The four pictograms stand for "
            "turtle, elephant, lion and cheetah.");
        measureSwitch.setTooltip("Measurement: On starts the measurement, the jewel lights up. "
            "While measuring, only fixed notches are active, dynamic ones are paused. "
            "Off stops the measurement and holds all detected notches.");
        resetButton.setTooltip("Reset: removes all fixed and dynamic notches. Hidden in Standalone mode.");
        bypassSwitch.setTooltip("Bypass: passes signal unprocessed. Notches remain active internally.");
        muteSwitch.setTooltip("Mute: output muted with a 5 ms ramp.");
        exportButton.setTooltip("Export: saves settings and fixed notches as XML.");
        importButton.setTooltip("Import: loads an XML preset including fixed notches. The current fixed notches are replaced.");
        sens.slider.setTooltip("Sensitivity: feedback detection sensitivity. Drag to change. Default 70 %.");
        hold.slider.setTooltip("Hold: time a notch stays after the last feedback hit. Drag to change.");
        release.slider.setTooltip("Release: time a notch takes to return to 0 dB. Drag to change.");
        hpf.slider.setTooltip("HPF: high-pass 36 dB/octave. A short click toggles on/off (green jewel). "
            "Drag with the mouse button held to set the cutoff frequency.");
        lpf.slider.setTooltip("LPF: low-pass 24 dB/octave. A short click toggles on/off (green jewel). "
            "Drag with the mouse button held to set the cutoff frequency.");
        cpuLabel.setInfoText("CPU: processing time relative to available block time.");
        versionLabel.setInfoText("Version: build version of the plugin.");
    }
}
void RoarRipperAudioProcessorEditor::updateVisibility()
{
    const int m = juce::roundToInt(modeSelector.getValue());
    const bool standalone = (m == 2);
    const bool ringOut = (m == 1);

    spectrum.setVisible(!standalone);
    exportButton.setVisible(!standalone);
    importButton.setVisible(!standalone);
    measureSwitch.setVisible(!standalone);   // Live und Statisch, nicht Standalone
    resetButton.setVisible(!standalone);

    for (auto* k : { &hold, &release, &hpf, &lpf })
    {
        k->slider.setVisible(!standalone);
        k->label.setVisible(!standalone);
    }

    // Speed bleibt in allen Modi sichtbar
    speedSelector.setVisible(true);
    speedLabel.setVisible(true);

    infoPopup.setVisible(false);
    hoverSeconds = 0.0;

    // ---- Fensterbreite pro Modus anfordern ----
    // Standalone = kompaktes Half-Rack (680), sonst 19" (1200).
    // Lehnt der Host die Aenderung ab, bleibt das alte Seitenverhaeltnis und
    // designWidthFor() faellt automatisch auf das breite Layout zurueck.
    const float targetDW = standalone ? DW_HALF : DW;
    const float scale = (uiScale > 0.05f) ? uiScale : 1.0f;

    if (standalone) setResizeLimits(510, 249, 1360, 664);
    else            setResizeLimits(900, 249, 1920, 531);
    getConstrainer()->setFixedAspectRatio((double)targetDW / (double)DH);

    const int nw = juce::roundToInt(targetDW * scale);
    const int nh = juce::roundToInt(DH * scale);
    if (nw != getWidth() || nh != getHeight())
        setSize(nw, nh);

    resized();
    repaint();
}

void RoarRipperAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit(0.001, 0.2, (now - lastTick) * 0.001);
    lastTick = now;

    vuIn.setLevelDb(proc.getInputLevel());
    vuOut.setLevelDb(proc.getOutputLevel());
    vuIn.update(dt);
    vuOut.update(dt);
    vuIn.repaint();
    vuOut.repaint();

    cpuLabel.setText("CPU " + juce::String(juce::roundToInt(proc.getCpuLoad() * 100.0f)) + " %",
        juce::dontSendNotification);

    measureSwitch.setToggleState(proc.isMeasuring(), juce::dontSendNotification);

    pollUpdate();

    hpf.slider.repaint();
    lpf.slider.repaint();

    if (spectrum.isVisible())
        spectrum.repaint();

    if (!lampRect.isEmpty())
        repaint(lampRect);

    updateInfoHover(dt);
}

void RoarRipperAudioProcessorEditor::pollUpdate()
{
#if RS_ENABLE_UPDATE_CHECK
    const auto info = rsupd::getInfo();

    if (!info.checked || !info.newer)
        return;

    if (!updateButton.isVisible())
    {
        updateButton.setToggleState(true, juce::dontSendNotification);   // leuchtet gruen
        updateButton.setVisible(true);
        setupInfoTexts();
        resized();
        repaint();
    }

    // Einmalige Meldung pro Prozess (nicht bei jedem Oeffnen des Editors)
    if (rsupd::claimNotification())
    {
        appendBugLog("Update verfuegbar: " + info.tag);

        const juce::String installed = "v" + getVersionString();

        const juce::String title = english ? "RS Roar Ripper update" : "RS Roar Ripper Update";
        const juce::String message = english
            ? "A new version of RS Roar Ripper is available: " + info.tag
                + " (installed: " + installed + ").\n\n"
                  "Use the green update button in the top bar of the plug-in to open the download page."
            : "Es ist eine neue Version von RS Roar Ripper verfuegbar: " + info.tag
                + " (installiert: " + installed + ").\n\n"
                  "Ueber den gruenen Update-Knopf in der Kopfzeile des Plug-ins gelangst Du zur Download-Seite.";

        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(title)
                .withMessage(message)
                .withButton(juce::MessageBoxButtonOptions("OK", 0)),   // JUCE 9: Button-Objekt statt Text
            [](int) {});
    }
#endif
}

void RoarRipperAudioProcessorEditor::updateInfoHover(double dt)
{
    auto ms = juce::Desktop::getInstance().getMainMouseSource();
    const auto sp = ms.getScreenPosition().roundToInt();
    auto* under = ms.getComponentUnderMouse();
    const bool inside = (under != nullptr) && (under == this || isParentOf(under));

    if (!inside || bugOverlay.isVisible())
    {
        hoverTarget = nullptr;
        hoverSeconds = 0.0;
        infoPopup.setVisible(false);
        lastMouse = sp;
        return;
    }

    if ((sp - lastMouse).getDistanceFromOrigin() > 2.0f)
    {
        hoverSeconds = 0.0;
        infoPopup.setVisible(false);
    }
    lastMouse = sp;

    juce::Component* found = nullptr;
    for (auto* c : infoTargets)
        if (c != nullptr && c->isShowing() && c->isMouseOver(true)) { found = c; break; }

    if (found != hoverTarget)
    {
        hoverTarget = found;
        hoverSeconds = 0.0;
        infoPopup.setVisible(false);
    }

    if (hoverTarget == nullptr)
        return;

    const bool exempt = (hoverTarget == &infoButton
        || hoverTarget == &coffeeButton
        || hoverTarget == &flagButton
        || hoverTarget == &updateButton);

    if (!infoMode && !exempt)
    {
        infoPopup.setVisible(false);
        return;
    }

    hoverSeconds += dt;
    if (hoverSeconds < 1.0)
        return;

    juce::String tip;
    if (auto* tc = dynamic_cast<juce::TooltipClient*>(hoverTarget))
        tip = tc->getTooltip();

    if (tip.isEmpty())
    {
        infoPopup.setVisible(false);
        return;
    }

    if (!infoPopup.isVisible())
        infoPopup.present(tip, getMouseXYRelative(), getLocalBounds(), uiScale);
}

// ---------------------------------------------------------------------------
void RoarRipperAudioProcessorEditor::buildPanel()
{
    const int W = juce::jmax(1, getWidth());
    const int H = juce::jmax(1, getHeight());
    const float dw = designWidthFor(W, H);

    panel = juce::Image(juce::Image::RGB, W, H, true);
    juce::Graphics g(panel);
    juce::Random rnd(20240711);
    const float s = (float)W / dw;

    auto full = juce::Rectangle<float>(0.0f, 0.0f, (float)W, (float)H);

    juce::ColourGradient base(rsui::panelHi, 0.0f, 0.0f, rsui::panelLo, 0.0f, (float)H, false);
    g.setGradientFill(base);
    g.fillRect(full);

    for (int y = 0; y < H; ++y)
    {
        const float a = 0.012f + rnd.nextFloat() * 0.06f;
        g.setColour((rnd.nextBool() ? rsui::cream : rsui::ink).withAlpha(a));
        g.drawHorizontalLine(y, 0.0f, (float)W);
    }

    for (int i = 0; i < 55; ++i)
    {
        const float x = rnd.nextFloat() * (float)W;
        const float y = rnd.nextFloat() * (float)H;
        const float rad = (20.0f + rnd.nextFloat() * 130.0f) * s;
        juce::ColourGradient st(juce::Colour(0xff6f6a4e).withAlpha(0.05f + rnd.nextFloat() * 0.08f),
            x, y, juce::Colour(0xff6f6a4e).withAlpha(0.0f), x + rad, y, true);
        g.setGradientFill(st);
        g.fillEllipse(x - rad, y - rad, rad * 2.0f, rad * 2.0f);
    }

    rsui::drawWear(g, full, rnd, 900, 1.35f);

    g.setColour(rsui::cream.withAlpha(0.28f));
    g.drawHorizontalLine(1, 0.0f, (float)W);
    g.drawHorizontalLine(2, 0.0f, (float)W);
    g.setColour(rsui::ink.withAlpha(0.34f));
    g.drawHorizontalLine(H - 2, 0.0f, (float)W);
    g.drawHorizontalLine(H - 3, 0.0f, (float)W);

    for (int i = 0; i < 4; ++i)
    {
        const float cx = (i % 2 == 0) ? 34.0f * s : (float)W - 34.0f * s;
        const float cy = (i < 2) ? 18.0f * s : (float)H - 18.0f * s;
        const float rad = 30.0f * s;
        juce::ColourGradient rub(rsui::cream.withAlpha(0.16f), cx, cy,
            rsui::cream.withAlpha(0.0f), cx + rad, cy, true);
        g.setGradientFill(rub);
        g.fillEllipse(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
    }

    const float ear = 26.0f * s;
    for (int side = 0; side < 2; ++side)
    {
        auto e = side == 0 ? juce::Rectangle<float>(0.0f, 0.0f, ear, (float)H)
            : juce::Rectangle<float>((float)W - ear, 0.0f, ear, (float)H);
        juce::ColourGradient eg(rsui::panelMid.darker(0.12f), e.getX(), e.getY(),
            rsui::panelLo.darker(0.25f), e.getX(), e.getBottom(), false);
        g.setGradientFill(eg);
        g.fillRect(e);
        g.setColour(rsui::ink.withAlpha(0.45f));
        g.drawVerticalLine((int)(side == 0 ? e.getRight() : e.getX()), 0.0f, (float)H);

        for (int i = 0; i < 2; ++i)
        {
            const float cy = (float)H * (i == 0 ? 0.22f : 0.78f);
            auto slot = juce::Rectangle<float>(e.getCentreX() - 6.0f * s, cy - 9.0f * s,
                12.0f * s, 18.0f * s);
            g.setColour(rsui::ink.withAlpha(0.80f));
            g.fillRoundedRectangle(slot, 5.0f * s);
            g.setColour(rsui::cream.withAlpha(0.18f));
            g.drawRoundedRectangle(slot.translated(0.0f, 1.0f), 5.0f * s, 1.0f);
        }
        rsui::drawWear(g, e, rnd, 70, 1.4f);
    }

    // Gehaeuseschrauben: rechts relativ zur aktuellen Designbreite
    rsui::drawScrew(g, { 40.0f * s,  16.0f * s }, 3.6f * s, rnd);
    rsui::drawScrew(g, { (dw - 40.0f) * s,  16.0f * s }, 3.6f * s, rnd);
    rsui::drawScrew(g, { 40.0f * s, 316.0f * s }, 3.6f * s, rnd);
    rsui::drawScrew(g, { (dw - 40.0f) * s, 316.0f * s }, 3.6f * s, rnd);

    auto groove = [&](float y)
        {
            g.setColour(rsui::ink.withAlpha(0.28f));
            g.drawHorizontalLine((int)(y * s), ear + 8.0f * s, (float)W - ear - 8.0f * s);
            g.setColour(rsui::cream.withAlpha(0.22f));
            g.drawHorizontalLine((int)(y * s) + 1, ear + 8.0f * s, (float)W - ear - 8.0f * s);
        };
    groove(50.0f);
    groove(300.0f);
}

void RoarRipperAudioProcessorEditor::paint(juce::Graphics& g)
{
    if (panel.isNull() || panel.getWidth() != getWidth() || panel.getHeight() != getHeight())
        buildPanel();

    g.drawImageAt(panel, 0, 0);

    const float s = uiScale;
    const bool standalone = (juce::roundToInt(modeSelector.getValue()) == 2);

    auto R = [&](float x, float y, float w, float h)
        {
            return juce::Rectangle<float>(x * s, y * s, w * s, h * s);
        };

    rsui::engravedText(g, "RS ROAR RIPPER", R(56.0f, 12.0f, 250.0f, 24.0f),
        juce::FontOptions(21.0f * s, juce::Font::bold),
        juce::Justification::centredLeft);

    // Modellzeile nur im breiten Layout
    if (!standalone)
        rsui::engravedText(g, "FEEDBACK SUPPRESSOR", R(312.0f, 18.0f, 220.0f, 14.0f),
            juce::FontOptions(9.0f * s, juce::Font::bold),
            juce::Justification::centredLeft);

    // Power-Jewel + Status
    {
        const float lampX = standalone ? 250.0f : 650.0f;
        const float statX = standalone ? 266.0f : 668.0f;
        const float statW = standalone ? 80.0f : 84.0f;

        auto lr = R(lampX, 18.0f, 12.0f, 12.0f);
        auto c = lr.getCentre();
        const float rad = lr.getWidth() * 0.5f;
        const bool muted = muteSwitch.getToggleState();
        const bool byp = bypassSwitch.getToggleState();
        auto col = muted ? rsui::jewelRed : (byp ? rsui::jewelAmb : rsui::jewelGrn);

        g.setColour(rsui::panelEdge);
        g.fillEllipse(c.x - rad - 1.5f * s, c.y - rad - 1.5f * s,
            (rad + 1.5f * s) * 2.0f, (rad + 1.5f * s) * 2.0f);
        juce::ColourGradient glow(col.withAlpha(0.40f), c.x, c.y,
            col.withAlpha(0.0f), c.x + rad * 3.0f, c.y, true);
        g.setGradientFill(glow);
        g.fillEllipse(c.x - rad * 3.0f, c.y - rad * 3.0f, rad * 6.0f, rad * 6.0f);
        g.setColour(col);
        g.fillEllipse(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour(rsui::cream.withAlpha(0.5f));
        g.fillEllipse(c.x - rad * 0.5f, c.y - rad * 0.7f, rad * 0.7f, rad * 0.5f);

        rsui::engravedText(g, muted ? "MUTE" : (byp ? "BYPASS" : "ACTIVE"),
            R(statX, 18.0f, statW, 12.0f),
            juce::FontOptions(8.5f * s, juce::Font::bold),
            juce::Justification::centredLeft);
    }

    rsui::engravedText(g, "ROARSOUND - MADE IN GERMANY - ZERO LATENCY",
        R(56.0f, 306.0f, 520.0f, 16.0f),
        juce::FontOptions(9.0f * s, juce::Font::bold),
        juce::Justification::centredLeft);
}

void RoarRipperAudioProcessorEditor::resized()
{
    const float dw = designWidthFor(getWidth(), getHeight());
    uiScale = (float)getWidth() / dw;

    const float s = uiScale;
    auto R = [&](float x, float y, float w, float h)
        {
            return juce::Rectangle<int>(juce::roundToInt(x * s), juce::roundToInt(y * s),
                juce::roundToInt(w * s), juce::roundToInt(h * s));
        };

    buildPanel();

    const bool standalone = (juce::roundToInt(modeSelector.getValue()) == 2);

    // ---- Kopfzeile: im Standalone endet sie bei 624 (= Output-VU / Speed) ----
    {
        float x = standalone ? 624.0f : 1144.0f;
        auto place = [&](juce::Component& c, float w)
            {
                x -= w;
                c.setBounds(R(x, 14.0f, w, 22.0f));
                x -= 6.0f;
            };
        place(bugButton, 22.0f);
        place(flagButton, 22.0f);
        place(infoButton, 22.0f);
        if (updateButton.isVisible())
            place(updateButton, 22.0f);
        x -= 4.0f;

        const float cpuW = standalone ? 60.0f : 74.0f;
        const float verW = standalone ? 46.0f : 54.0f;
        cpuLabel.setBounds(R(x - cpuW, 18.0f, cpuW, 14.0f));
        x -= cpuW + 8.0f;
        versionLabel.setBounds(R(x - verW, 18.0f, verW, 14.0f));
    }

    coffeeButton.setBounds(standalone ? R(353.0f, 3.0f, 44.0f, 44.0f)
        : R(DW * 0.5f - 22.0f, 3.0f, 44.0f, 44.0f));

    lampRect = standalone ? R(244.0f, 12.0f, 110.0f, 26.0f)
        : R(644.0f, 12.0f, 120.0f, 26.0f);

    // ---- Immer ortsfest: Input-VU, Mode-Schalter, Reset-Position ----
    vuIn.setBounds(R(56.0f, 54.0f, 188.0f, 126.0f));
    modeSelector.setBounds(R(56.0f, 184.0f, 164.0f, 112.0f));
    resetButton.setBounds(R(230.0f, 184.0f, 52.0f, 20.0f));

    const float rowY = 208.0f, rowH = 88.0f;

    auto placeToggle = [&](ToggleSwitch& t, float x)
        {
            t.setBounds(R(x, rowY, 52.0f, rowH));
        };

    auto placeKnob = [&](Knob& k, float x, float w)
        {
            auto cell = R(x, rowY, w, rowH);
            k.label.setFont(juce::FontOptions(9.0f * s, juce::Font::bold));
            k.label.setBounds(cell.removeFromTop(juce::roundToInt(13.0f * s)));
            k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                juce::roundToInt((w - 26.0f) * s),
                juce::roundToInt(15.0f * s));
            k.slider.setBounds(cell);
        };

    // Beschriftung des Speed-Waehlers skaliert mit dem Fenster
    speedLabel.setFont(juce::FontOptions(9.0f * s, juce::Font::bold));

    if (!standalone)
    {
        // ---- Referenz-Layout (Einpfeifen / Live) ----
        spectrum.setBounds(R(256.0f, 54.0f, 688.0f, 126.0f));
        vuOut.setBounds(R(956.0f, 54.0f, 188.0f, 126.0f));

        placeToggle(measureSwitch, 230.0f);
        placeToggle(bypassSwitch, 286.0f);
        placeToggle(muteSwitch, 342.0f);

        placeKnob(hpf, 404.0f, 100.0f);
        placeKnob(lpf, 512.0f, 100.0f);
        placeKnob(sens, 622.0f, 92.0f);
        placeKnob(hold, 722.0f, 92.0f);
        placeKnob(release, 822.0f, 92.0f);

        speedLabel.setBounds(R(926.0f, 190.0f, 172.0f, 14.0f));
        speedSelector.setBounds(R(926.0f, rowY, 172.0f, rowH));
    }
    else
    {
        // ---- Standalone: Half-Rack, alles nach links aufgerueckt ----
        spectrum.setBounds({});

        placeToggle(measureSwitch, 230.0f);   // unsichtbar, Referenzposition
        placeToggle(bypassSwitch, 230.0f);
        placeToggle(muteSwitch, 286.0f);

        placeKnob(sens, 348.0f, 92.0f);
        placeKnob(hpf, 404.0f, 100.0f);   // unsichtbar
        placeKnob(lpf, 512.0f, 100.0f);   // unsichtbar
        placeKnob(hold, 722.0f, 92.0f);   // unsichtbar
        placeKnob(release, 822.0f, 92.0f);   // unsichtbar

        speedLabel.setBounds(R(452.0f, 190.0f, 172.0f, 14.0f));
        speedSelector.setBounds(R(452.0f, rowY, 172.0f, rowH));

        // Output-VU: rechte Kante = rechte Kante Speed (452 + 172 = 624)
        vuOut.setBounds(R(436.0f, 54.0f, 188.0f, 126.0f));
    }

    // ---- Fusszeile ----
    {
        float fx = standalone ? 624.0f : 1144.0f;
        auto placeBtn = [&](juce::Component& c)
            {
                if (!c.isVisible()) return;
                fx -= 76.0f;
                c.setBounds(R(fx, 304.0f, 76.0f, 20.0f));
                fx -= 8.0f;
            };
        placeBtn(importButton);
        placeBtn(exportButton);
    }

    bugOverlay.setBounds(getLocalBounds());
}