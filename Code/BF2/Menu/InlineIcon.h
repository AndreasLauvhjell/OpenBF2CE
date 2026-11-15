// [CHOSEN, not recovered] Local experiment, no issue yet: inline icons in list box text.
// A section sign, 'I' and four hex digits draw a registered texture in the text flow, so a kill line
// can show a weapon icon between the player names. Retail list boxes know only the font (0-3) and
// colour (C) escapes and would print this escape as text.
#ifndef MENU_INLINEICON_H
#define MENU_INLINEICON_H

#include "../External/BFMeme/System.h"
#include <string>
#include <utility>
#include <vector>

namespace dice { namespace meme {

// A texture registered for inline drawing, with its pixel size.
struct InlineIcon
{
    std::string path;
    int texture;
    float width;
    float height;
};

inline std::vector<InlineIcon>& inlineIcons()
{
    static std::vector<InlineIcon> icons;
    return icons;
}

// Returns the slot for a texture path, or -1 while the texture has no size (missing file). The
// texture is loaded once; a texture without a size is asked again on the next registration.
inline int registerInlineIcon(System* system, const std::string& path)
{
    if (!system || path.empty()) return -1;
    std::vector<InlineIcon>& icons = inlineIcons();
    std::size_t slot = 0;
    while (slot < icons.size() && icons[slot].path != path) ++slot;
    if (slot == icons.size()) {
        if (icons.size() > 0xFFFF) return -1;
        InlineIcon icon;
        icon.path = path;
        icon.texture = system->loadTexture(path.c_str(), true);
        icon.width = icon.height = 0.0f;
        icons.push_back(icon);
    }
    InlineIcon& icon = icons[slot];
    if (icon.texture && (icon.width <= 0.0f || icon.height <= 0.0f)) {
        icon.width = system->getTextureWidth(icon.texture);
        icon.height = system->getTextureHeight(icon.texture);
    }
    return icon.texture && icon.width > 0.0f && icon.height > 0.0f ? static_cast<int>(slot) : -1;
}

inline const InlineIcon* findInlineIcon(int slot)
{
    std::vector<InlineIcon>& icons = inlineIcons();
    if (slot < 0 || static_cast<std::size_t>(slot) >= icons.size()) return nullptr;
    const InlineIcon& icon = icons[slot];
    return icon.texture && icon.width > 0.0f && icon.height > 0.0f ? &icon : nullptr;
}

// The escape text for a slot: section sign, 'I', four upper-case hex digits.
inline std::wstring inlineIconEscape(int slot)
{
    std::wstring text(L"\xA7I");
    for (int shift = 12; shift >= 0; shift -= 4)
        text += L"0123456789ABCDEF"[(slot >> shift) & 0xF];
    return text;
}

// Reads the four hex digits after the escape through get(0..3). Returns the icon, or null when a
// digit is malformed or the slot is unknown.
template<class Get> const InlineIcon* readInlineIcon(Get get)
{
    int slot = 0;
    for (int i = 0; i < 4; ++i) {
        const int c = get(i);
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else return nullptr;
        slot = slot * 16 + digit;
    }
    return findInlineIcon(slot);
}

// Drawn size: the icon keeps its aspect and shrinks to the row height, never grows.
inline std::pair<float, float> inlineIconSize(const InlineIcon& icon, float rowHeight)
{
    float scale = 1.0f;
    if (rowHeight > 0.0f && icon.height > rowHeight) scale = rowHeight / icon.height;
    return std::make_pair(icon.width * scale, icon.height * scale);
}

// Horizontal space an icon takes in the text, including a one-pixel gap on each side.
inline float inlineIconAdvance(const InlineIcon& icon, float rowHeight)
{
    return inlineIconSize(icon, rowHeight).first + 2.0f;
}

} }

#endif // MENU_INLINEICON_H
