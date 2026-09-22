#pragma once

#include <QChar>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

/**
 * Colour arithmetic and the curated palette choices.
 *
 * This file deliberately carries no bulk-imported colour catalog: only the curated
 * palettes below travel with the application. Pastels and Grayscale are original to this
 * project; every other set reuses its upstream project's exact published hex values
 * under the licence named in each entry. An optional local-only bundle is still
 * reachable at runtime through `ThemeCatalog`, which validates and namespaces it
 * separately. Nothing here reads it.
 *
 * Ink is DERIVED from paper rather than stored, so a hand-edited manifest cannot produce
 * an unreadable note. The one exception is an explicit per-note ink, which is the user's
 * own decision and is returned unchanged even when it is low contrast.
 */
namespace Palette {

/** True for `#rgb` or `#rrggbb`, case-insensitive. */
inline bool validColor(const QString &value)
{
    if (value.size() != 4 && value.size() != 7) {
        return false;
    }
    if (!value.startsWith(QLatin1Char('#'))) {
        return false;
    }
    for (qsizetype index = 1; index < value.size(); ++index) {
        if (!std::isxdigit(static_cast<unsigned char>(value.at(index).toLatin1()))) {
            return false;
        }
    }
    return true;
}

/** Expand `#rgb` and lower-case, so one colour has exactly one stored spelling and
 *  equality against a palette swatch is a plain string compare. */
inline QString normalizeColor(const QString &value)
{
    if (!validColor(value)) {
        return {};
    }
    QString hex = value.mid(1).toLower();
    if (hex.size() == 3) {
        hex = QStringLiteral("%1%1%2%2%3%3").arg(hex.at(0)).arg(hex.at(1)).arg(hex.at(2));
    }
    return QLatin1Char('#') + hex;
}

inline bool parseColor(const QString &value, int &r, int &g, int &b)
{
    const QString hex = normalizeColor(value);
    if (hex.isEmpty()) {
        return false;
    }
    r = hex.mid(1, 2).toInt(nullptr, 16);
    g = hex.mid(3, 2).toInt(nullptr, 16);
    b = hex.mid(5, 2).toInt(nullptr, 16);
    return true;
}

inline QString hexOf(int r, int g, int b)
{
    return QStringLiteral("#%1%2%3")
        .arg(qBound(0, r, 255), 2, 16, QLatin1Char('0'))
        .arg(qBound(0, g, 255), 2, 16, QLatin1Char('0'))
        .arg(qBound(0, b, 255), 2, 16, QLatin1Char('0'));
}

/** WCAG 2.1 relative luminance of an sRGB triple. */
inline double luminance(int r, int g, int b)
{
    const auto linear = [](int v) {
        const double c = v / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
}

/** WCAG contrast ratio, 1.0 (identical) to 21.0 (black on white). */
inline double contrast(int r1, int g1, int b1, int r2, int g2, int b2)
{
    double a = luminance(r1, g1, b1);
    double c = luminance(r2, g2, b2);
    if (a < c) {
        std::swap(a, c);
    }
    return (a + 0.05) / (c + 0.05);
}

inline constexpr double inkContrastTarget = 7.0;

/** Compute readable ink for one paper colour.
 *
 *  The ink walks from the paper towards black (light paper) or white (dark paper) along a
 *  straight line in sRGB, which keeps the paper's hue instead of dropping to a flat grey,
 *  and stops at the first step reaching WCAG AAA body contrast (7:1) where attainable.
 *  Mid-luminance papers cannot reach 7:1 with any ink; for those the better black/white
 *  endpoint guarantees WCAG AA (at least 4.5:1).
 */
inline QString inkFor(const QString &paperColor)
{
    int r = 0;
    int g = 0;
    int b = 0;
    if (!parseColor(paperColor, r, g, b)) {
        return QStringLiteral("#1b1b1f");
    }
    const bool towardsBlack = contrast(0, 0, 0, r, g, b) >= contrast(255, 255, 255, r, g, b);
    for (int step = 1; step <= 100; ++step) {
        const double f = step / 100.0;
        const int ir = qRound(towardsBlack ? r * (1.0 - f) : r + (255 - r) * f);
        const int ig = qRound(towardsBlack ? g * (1.0 - f) : g + (255 - g) * f);
        const int ib = qRound(towardsBlack ? b * (1.0 - f) : b + (255 - b) * f);
        if (contrast(ir, ig, ib, r, g, b) >= inkContrastTarget) {
            return hexOf(ir, ig, ib);
        }
    }
    return towardsBlack ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
}

/** The sentinel meaning "derive this note's ink from its paper". */
inline QString autoInk()
{
    return QStringLiteral("auto");
}

/** Normalize a stored ink: the sentinel, a canonical literal, or empty for junk. */
inline QString normalizeInk(const QString &value)
{
    if (value == autoInk()) {
        return autoInk();
    }
    return normalizeColor(value);
}

/** Resolve one note's painted ink.
 *
 *  "auto" derives the strongest readable ink from the paper. An explicit literal is the
 *  user's own decision and is returned UNCHANGED even when it is low contrast: a
 *  deliberate choice is not rejected, second-guessed or silently corrected. Only the
 *  automatic branch carries a contrast guarantee.
 */
inline QString resolveInk(const QString &paperColor, const QString &inkValue)
{
    const QString stored = normalizeInk(inkValue);
    if (stored.isEmpty() || stored == autoInk()) {
        return inkFor(paperColor);
    }
    return stored;
}

/** The named palettes offered by the global Palette control.
 *
 * A palette supplies candidate paper colours and nothing else. It is never recorded
 * against a note, so changing it cannot repaint anything already assigned. Each entry
 * carries its own provenance and licence text, displayed verbatim by the settings panel.
 *
 * Paper values are the upstream projects' EXACT published hex values; no upstream colour
 * is renamed, altered or invented. The accompanying ink is computed here by inkFor()
 * rather than taken from upstream, because upstream assigns these colours as accents on
 * their own backgrounds, not as paper.
 */
inline QVariantList curatedPalettes()
{
    const auto entry = [](const QString &key, const QString &label, const QString &group,
                          const QString &source, const QString &license,
                          const QStringList &colors) {
        QVariantList swatches;
        for (const QString &c : colors) {
            swatches.append(QVariantMap{{QStringLiteral("paper"), c},
                                        {QStringLiteral("ink"), inkFor(c)},
                                        {QStringLiteral("group"), QStringLiteral("curated")},
                                        {QStringLiteral("role"), c},
                                        {QStringLiteral("label"), c}});
        }
        return QVariantMap{{QStringLiteral("key"), key},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("group"), group},
                           {QStringLiteral("source"), source},
                           {QStringLiteral("license"), license},
                           {QStringLiteral("swatches"), swatches}};
    };
    // Two groups. "ColorBrewer 2.0" is the eight qualitative sets from colorbrewer2.org
    // under project-chosen one-word names (the upstream names Set1/Pastel2/... describe
    // nothing); "Popular Themes" is the editor-theme canon plus this project's own
    // Pastels and Grayscale. Ink is derived per swatch by inkFor() to a WCAG AAA (7:1)
    // target.
    return {
        entry(QStringLiteral("cb-set1"), QStringLiteral("Bold"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Set1."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#e41a1c"), QStringLiteral("#377eb8"), QStringLiteral("#4daf4a"),
               QStringLiteral("#984ea3"), QStringLiteral("#ff7f00"), QStringLiteral("#ffff33"),
               QStringLiteral("#a65628"), QStringLiteral("#f781bf")}),
        entry(QStringLiteral("cb-set2"), QStringLiteral("Muted"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Set2."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#66c2a5"), QStringLiteral("#fc8d62"), QStringLiteral("#8da0cb"),
               QStringLiteral("#e78ac3"), QStringLiteral("#a6d854"), QStringLiteral("#ffd92f"),
               QStringLiteral("#e5c494"), QStringLiteral("#b3b3b3")}),
        entry(QStringLiteral("cb-set3"), QStringLiteral("Light"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Set3."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#8dd3c7"), QStringLiteral("#ffffb3"), QStringLiteral("#bebada"),
               QStringLiteral("#fb8072"), QStringLiteral("#80b1d3"), QStringLiteral("#fdb462"),
               QStringLiteral("#b3de69"), QStringLiteral("#fccde5")}),
        entry(QStringLiteral("cb-pastel1"), QStringLiteral("Pastel"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Pastel1."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#fbb4ae"), QStringLiteral("#b3cde3"), QStringLiteral("#ccebc5"),
               QStringLiteral("#decbe4"), QStringLiteral("#fed9a6"), QStringLiteral("#ffffcc"),
               QStringLiteral("#e5d8bd"), QStringLiteral("#fddaec")}),
        entry(QStringLiteral("cb-pastel2"), QStringLiteral("Soft"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Pastel2."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#b3e2cd"), QStringLiteral("#fdcdac"), QStringLiteral("#cbd5e8"),
               QStringLiteral("#f4cae4"), QStringLiteral("#e6f5c9"), QStringLiteral("#fff2ae"),
               QStringLiteral("#f1e2cc"), QStringLiteral("#cccccc")}),
        entry(QStringLiteral("cb-dark2"), QStringLiteral("Deep"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Dark2."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#1b9e77"), QStringLiteral("#d95f02"), QStringLiteral("#7570b3"),
               QStringLiteral("#e7298a"), QStringLiteral("#66a61e"), QStringLiteral("#e6ab02"),
               QStringLiteral("#a6761d"), QStringLiteral("#666666")}),
        entry(QStringLiteral("cb-paired"), QStringLiteral("Paired"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Paired."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#a6cee3"), QStringLiteral("#1f78b4"), QStringLiteral("#b2df8a"),
               QStringLiteral("#33a02c"), QStringLiteral("#fb9a99"), QStringLiteral("#e31a1c"),
               QStringLiteral("#fdbf6f"), QStringLiteral("#ff7f00")}),
        entry(QStringLiteral("cb-accent"), QStringLiteral("Highlight"),
              QStringLiteral("ColorBrewer 2.0"),
              QStringLiteral("ColorBrewer 2.0 — colorbrewer2.org, Cynthia Brewer, Mark Harrower and The Pennsylvania State University; qualitative scheme Accent."),
              QStringLiteral("Apache License 2.0 (ColorBrewer)."),
              {QStringLiteral("#7fc97f"), QStringLiteral("#beaed4"), QStringLiteral("#fdc086"),
               QStringLiteral("#ffff99"), QStringLiteral("#386cb0"), QStringLiteral("#f0027f"),
               QStringLiteral("#bf5b17"), QStringLiteral("#666666")}),
        entry(QStringLiteral("pastels"), QStringLiteral("Pastels"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Original to this project."),
              QStringLiteral("Same licence as this repository."),
              {QStringLiteral("#f5f0e6"), QStringLiteral("#f2ece0"), QStringLiteral("#f7f2e9"),
               QStringLiteral("#efe6d4"), QStringLiteral("#e8f0e6"), QStringLiteral("#e6eef2"),
               QStringLiteral("#f2e6ee"), QStringLiteral("#efe9f4")}),
        entry(QStringLiteral("grayscale"), QStringLiteral("Grayscale"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Plain grey steps, light to near-black; original to this project."),
              QStringLiteral("Same licence as this repository."),
              {QStringLiteral("#f2f2f2"), QStringLiteral("#d9d9d9"), QStringLiteral("#bfbfbf"),
               QStringLiteral("#a6a6a6"), QStringLiteral("#8c8c8c"), QStringLiteral("#595959"),
               QStringLiteral("#333333"), QStringLiteral("#1a1a1a")}),
        entry(QStringLiteral("nord"), QStringLiteral("Nord"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Nord — nordtheme/nord, official Frost and Aurora accents."),
              QStringLiteral("MIT License, Copyright (c) 2016-present Sven Greb."),
              {QStringLiteral("#bf616a"), QStringLiteral("#d08770"), QStringLiteral("#81a1c1"),
               QStringLiteral("#8fbcbb"), QStringLiteral("#88c0d0"), QStringLiteral("#ebcb8b"),
               QStringLiteral("#a3be8c"), QStringLiteral("#b48ead")}),
        entry(QStringLiteral("mocha"), QStringLiteral("Catppuccin"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Catppuccin Mocha — catppuccin/catppuccin, official palette."),
              QStringLiteral("MIT License, Copyright (c) 2021 Catppuccin."),
              {QStringLiteral("#f5e0dc"), QStringLiteral("#f2cdcd"), QStringLiteral("#f5c2e7"),
               QStringLiteral("#cba6f7"), QStringLiteral("#fab387"), QStringLiteral("#f9e2af"),
               QStringLiteral("#a6e3a1"), QStringLiteral("#94e2d5")}),
        entry(QStringLiteral("solarized"), QStringLiteral("Solarized"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Solarized — Ethan Schoonover's official base tones, light to dark."),
              QStringLiteral("MIT License, Copyright (c) 2011 Ethan Schoonover."),
              {QStringLiteral("#fdf6e3"), QStringLiteral("#eee8d5"), QStringLiteral("#93a1a1"),
               QStringLiteral("#839496"), QStringLiteral("#657b83"), QStringLiteral("#586e75"),
               QStringLiteral("#073642"), QStringLiteral("#002b36")}),
        entry(QStringLiteral("gruvbox"), QStringLiteral("Gruvbox"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Gruvbox — morhetz/gruvbox, official background steps."),
              QStringLiteral("MIT License, Copyright (c) 2012 Pavel Pertsev."),
              {QStringLiteral("#fbf1c7"), QStringLiteral("#ebdbb2"), QStringLiteral("#d5c4a1"),
               QStringLiteral("#a89984"), QStringLiteral("#665c54"), QStringLiteral("#504945"),
               QStringLiteral("#3c3836"), QStringLiteral("#282828")}),
        entry(QStringLiteral("dracula"), QStringLiteral("Dracula"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Dracula — dracula/dracula-theme, official palette."),
              QStringLiteral("MIT License, Copyright (c) 2016 Dracula Theme."),
              {QStringLiteral("#282a36"), QStringLiteral("#44475a"), QStringLiteral("#8be9fd"),
               QStringLiteral("#50fa7b"), QStringLiteral("#ffb86c"), QStringLiteral("#ff79c6"),
               QStringLiteral("#bd93f9"), QStringLiteral("#f1fa8c")}),
        entry(QStringLiteral("monokai"), QStringLiteral("Monokai"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Monokai — Wimer Hazenberg's classic scheme, official accents on its dark ground."),
              QStringLiteral("Free to use; colours from the published scheme."),
              {QStringLiteral("#272822"), QStringLiteral("#f92672"), QStringLiteral("#a6e22e"),
               QStringLiteral("#fd971f"), QStringLiteral("#66d9ef"), QStringLiteral("#ae81ff"),
               QStringLiteral("#e6db74"), QStringLiteral("#f8f8f2")}),
        entry(QStringLiteral("onedark"), QStringLiteral("One Dark"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("One Dark — Atom's official syntax theme palette."),
              QStringLiteral("MIT License, Copyright (c) 2016 GitHub Inc."),
              {QStringLiteral("#282c34"), QStringLiteral("#e06c75"), QStringLiteral("#98c379"),
               QStringLiteral("#e5c07b"), QStringLiteral("#61afef"), QStringLiteral("#c678dd"),
               QStringLiteral("#56b6c2"), QStringLiteral("#abb2bf")}),
        entry(QStringLiteral("tokyonight"), QStringLiteral("Tokyo Night"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Tokyo Night — folke/tokyonight.nvim, official night palette."),
              QStringLiteral("Apache License 2.0, Copyright (c) folke."),
              {QStringLiteral("#1a1b26"), QStringLiteral("#f7768e"), QStringLiteral("#ff9e64"),
               QStringLiteral("#e0af68"), QStringLiteral("#9ece6a"), QStringLiteral("#7dcfff"),
               QStringLiteral("#7aa2f7"), QStringLiteral("#bb9af7")}),
        entry(QStringLiteral("ayu"), QStringLiteral("Ayu"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Ayu — ayu-theme/ayu-colors, official dark accents."),
              QStringLiteral("MIT License, Copyright (c) 2016 Ike Ku."),
              {QStringLiteral("#0d1017"), QStringLiteral("#f07178"), QStringLiteral("#ff8f40"),
               QStringLiteral("#e6b450"), QStringLiteral("#aad94c"), QStringLiteral("#95e6cb"),
               QStringLiteral("#39bae6"), QStringLiteral("#59c2ff")}),
        entry(QStringLiteral("everforest"), QStringLiteral("Everforest"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Everforest — sainnhe/everforest, official dark palette."),
              QStringLiteral("MIT License, Copyright (c) 2020 sainnhe."),
              {QStringLiteral("#2d353b"), QStringLiteral("#e67e80"), QStringLiteral("#e69875"),
               QStringLiteral("#dbbc7f"), QStringLiteral("#a7c080"), QStringLiteral("#83c092"),
               QStringLiteral("#7fbbb3"), QStringLiteral("#d699b6")}),
        entry(QStringLiteral("rosepine"), QStringLiteral("Rosé Pine"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Rosé Pine — rose-pine/rose-pine-theme, official base palette."),
              QStringLiteral("MIT License, Copyright (c) 2021 Rosé Pine."),
              {QStringLiteral("#191724"), QStringLiteral("#eb6f92"), QStringLiteral("#f6c177"),
               QStringLiteral("#ebbcba"), QStringLiteral("#31748f"), QStringLiteral("#9ccfd8"),
               QStringLiteral("#c4a7e7"), QStringLiteral("#e0def4")}),
        entry(QStringLiteral("material"), QStringLiteral("Material"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Material Theme — the classic Material palette for editors."),
              QStringLiteral("MIT License, Copyright (c) Mattia Astorino."),
              {QStringLiteral("#263238"), QStringLiteral("#f07178"), QStringLiteral("#f78c6c"),
               QStringLiteral("#ffcb6b"), QStringLiteral("#c3e88d"), QStringLiteral("#89ddff"),
               QStringLiteral("#82aaff"), QStringLiteral("#c792ea")}),
        entry(QStringLiteral("kanagawa"), QStringLiteral("Kanagawa"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Kanagawa — rebelot/kanagawa.nvim, official wave palette."),
              QStringLiteral("MIT License, Copyright (c) 2021 rebelot."),
              {QStringLiteral("#1f1f28"), QStringLiteral("#ff5d62"), QStringLiteral("#ffa066"),
               QStringLiteral("#e6c384"), QStringLiteral("#98bb6c"), QStringLiteral("#7fb4ca"),
               QStringLiteral("#7e9cd8"), QStringLiteral("#d27e99")}),
        entry(QStringLiteral("github"), QStringLiteral("GitHub"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("GitHub Primer — primer/primitives, official scale colours."),
              QStringLiteral("MIT License, Copyright (c) GitHub Inc."),
              {QStringLiteral("#f6f8fa"), QStringLiteral("#cf222e"), QStringLiteral("#bc4c00"),
               QStringLiteral("#bf8700"), QStringLiteral("#1a7f37"), QStringLiteral("#0969da"),
               QStringLiteral("#8250df"), QStringLiteral("#0d1117")}),
        entry(QStringLiteral("nightfox"), QStringLiteral("Nightfox"),
              QStringLiteral("Popular Themes"),
              QStringLiteral("Nightfox — EdenEast/nightfox.nvim, official palette."),
              QStringLiteral("MIT License, Copyright (c) 2021 EdenEast."),
              {QStringLiteral("#192330"), QStringLiteral("#c94f6d"), QStringLiteral("#f4a261"),
               QStringLiteral("#dbc074"), QStringLiteral("#81b29a"), QStringLiteral("#63cdcf"),
               QStringLiteral("#719cd6"), QStringLiteral("#9d79d6")}),
    };
}

/** Labels and keys only; swatches are built on demand by paletteOf(). */
inline QVariantList paletteChoices()
{
    QVariantList out;
    for (const QVariant &p : curatedPalettes()) {
        const QVariantMap map = p.toMap();
        // The literal paper colours ride along so a chooser can PAINT each palette
        // instead of asking the user to know what "Muted" holds.
        QStringList colors;
        for (const QVariant &sw : map.value(QStringLiteral("swatches")).toList()) {
            colors.append(sw.toMap().value(QStringLiteral("paper")).toString());
        }
        out.append(QVariantMap{{QStringLiteral("key"), map.value(QStringLiteral("key"))},
                               {QStringLiteral("label"), map.value(QStringLiteral("label"))},
                               {QStringLiteral("group"), map.value(QStringLiteral("group"))},
                               {QStringLiteral("colors"), colors},
                               {QStringLiteral("search"),
                                map.value(QStringLiteral("label")).toString().toLower()}});
    }
    return out;
}

inline QStringList paletteKeys()
{
    QStringList keys;
    for (const QVariant &p : paletteChoices()) {
        keys.append(p.toMap().value(QStringLiteral("key")).toString());
    }
    return keys;
}

/** @return the full record, with swatches, for one palette key. An unknown key falls back
 *  to the first curated palette WITHOUT recolouring anything. */
inline QVariantMap paletteOf(const QString &key)
{
    for (const QVariant &p : curatedPalettes()) {
        if (p.toMap().value(QStringLiteral("key")).toString() == key) {
            return p.toMap();
        }
    }
    return curatedPalettes().first().toMap();
}

/** @return the literal paper colours one palette offers. */
inline QStringList paletteColors(const QString &key)
{
    QStringList colors;
    for (const QVariant &s : paletteOf(key).value(QStringLiteral("swatches")).toList()) {
        colors.append(s.toMap().value(QStringLiteral("paper")).toString());
    }
    return colors;
}

/** The paper assigned to a note that has never been given one. */
inline QString defaultPaper()
{
    return QStringLiteral("#f5f0e6");
}

} // namespace Palette
