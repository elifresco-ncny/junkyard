#include "DamageScope.h"

namespace jy::ui
{
DamageScope::DamageScope() { setOpaque (false); setTooltip ("Spectrum of the codec output over time. Blue: kept. Dark: removed. Red: glitched frames."); }

void DamageScope::clear() { img.clear (img.getBounds(), juce::Colours::black); }

void DamageScope::pull (ScopeRing& ring)
{
    ScopeFrame f;
    int n = 0;
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    while (ring.pop (f) && n < 4 * kCols)
    {
        ++n;
        for (int i = 0; i < kScopeBins; ++i)
        {
            const int row = kScopeBins - 1 - i; // low frequencies at the bottom
            const int o = f.orig[i], k = f.kept[i];
            juce::Colour c = juce::Colours::black;
            const bool live = o > 40;                 // original has something (> ~ -84 dB)
            if (k > 20) { const float t = juce::jlimit (0.0f, 1.0f, (float) k / 230.0f); c = juce::Colour::fromHSV (0.60f - 0.06f * t, 0.85f - 0.35f * t, 0.15f + 0.85f * t, 1.0f); }
            else if (live) c = juce::Colour (0xff050608); // hole
            if (f.glitch) c = c.interpolatedWith (red, 0.55f);
            bd.setPixelColour (col, row, c);
        }
        col = (col + 1) % kCols;
    }
    if (n > 0) repaint();
}

void DamageScope::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black); g.fillRoundedRectangle (b, 6.0f);
    auto inner = b.reduced (3.0f);
    g.saveState();
    juce::Path clip; clip.addRoundedRectangle (inner, 4.0f); g.reduceClipRegion (clip);
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    // two segments of the circular image: [col, kCols) is oldest, [0, col) newest
    const float wOld = inner.getWidth() * (float) (kCols - col) / kCols;
    if (col < kCols)
        g.drawImage (img, (int) inner.getX(), (int) inner.getY(), (int) std::ceil (wOld), (int) inner.getHeight(), col, 0, kCols - col, kScopeBins);
    if (col > 0)
        g.drawImage (img, (int) (inner.getX() + wOld), (int) inner.getY(), (int) std::ceil (inner.getWidth() - wOld), (int) inner.getHeight(), 0, 0, col, kScopeBins);
    g.restoreState();
    g.setColour (edge); g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
    if (auto* l = dynamic_cast<JunkyardLookAndFeel*> (&getLookAndFeel()))
    {
        g.setColour (textDim.withAlpha (0.8f)); g.setFont (l->mono (10.0f));
        g.drawText ("DAMAGE SCOPE", inner.toNearestInt().reduced (6, 3), juce::Justification::topLeft, false);
    }
}
} // namespace jy::ui
