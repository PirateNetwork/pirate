// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_PLATFORMSTYLE_H
#define KOMODO_QT_PLATFORMSTYLE_H

#include <QIcon>
#include <QPixmap>
#include <QString>

/* Coin network-specific GUI style information */
class PlatformStyle
{
public:
    /** Get style associated with provided platform name, or 0 if not known */
    static const PlatformStyle *instantiate(const QString &platformId);

    const QString &getName() const { return name; }

    bool getImagesOnButtons() const { return imagesOnButtons; }
    bool getUseExtraSpacing() const { return useExtraSpacing; }

    QColor TextColor() const { return textColor; }
    QColor SingleColor() const { return singleColor; }

    /** Retint icons produced by SingleColorIcon()/SingleColorImage() to match
     * the active theme (dark theme icons want a light neutral to read on a
     * dark surface, light theme icons want a dark neutral). Icons already
     * created keep their old color -- callers that build icons once at
     * startup (e.g. the toolbar actions) need to recreate them after calling
     * this to see the new tint. const because every call site holds a
     * `const PlatformStyle *` (it's handed out that way from instantiate());
     * singleColor is the one piece of paint-time state that legitimately
     * changes after construction, hence mutable. */
    void setSingleColor(const QColor &color) const { singleColor = color; }

    /** Colorize an image (given filename) with the icon color */
    QImage SingleColorImage(const QString& filename) const;

    /** Colorize an icon (given filename) with the icon color */
    QIcon SingleColorIcon(const QString& filename) const;

    /** Colorize an icon (given object) with the icon color */
    QIcon SingleColorIcon(const QIcon& icon) const;

    /** Colorize an icon (given filename) with the text color */
    QIcon TextColorIcon(const QString& filename) const;

    /** Colorize an icon (given object) with the text color */
    QIcon TextColorIcon(const QIcon& icon) const;

private:
    PlatformStyle(const QString &name, bool imagesOnButtons, bool colorizeIcons, bool useExtraSpacing);

    QString name;
    bool imagesOnButtons;
    bool colorizeIcons;
    bool useExtraSpacing;
    mutable QColor singleColor;
    QColor textColor;
    /* ... more to come later */
};

#endif // KOMODO_QT_PLATFORMSTYLE_H

