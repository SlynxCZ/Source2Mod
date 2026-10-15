/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "consoletable.h"

#include <algorithm>
#include <cstdint>

namespace consoletable {
    namespace {
        // One step through the text: an escape sequence, or one UTF-8 code
        // point (a broken byte counts as one column, like a terminal shows it).
        struct Piece
        {
            size_t length;      // bytes
            int width;          // columns
        };

        int CodePointWidth(uint32_t cp)
        {
            if (cp == 0)
                return 0;
            if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
                return 0;

            // Combining marks, zero-width spaces/joiners, variation selectors.
            if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0483 && cp <= 0x0489) ||
                (cp >= 0x0591 && cp <= 0x05BD) || (cp >= 0x0610 && cp <= 0x061A) ||
                (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
                (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x200B && cp <= 0x200F) ||
                (cp >= 0x2028 && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x2064) ||
                (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
                (cp >= 0xFE20 && cp <= 0xFE2F) || cp == 0xFEFF ||
                (cp >= 0xE0100 && cp <= 0xE01EF))
                return 0;

            // East Asian wide and fullwidth, and the emoji blocks.
            if ((cp >= 0x1100 && cp <= 0x115F) || cp == 0x2329 || cp == 0x232A ||
                (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
                (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
                (cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
                (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
                (cp >= 0x1F300 && cp <= 0x1F64F) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
                (cp >= 0x20000 && cp <= 0x3FFFD))
                return 2;

            return 1;
        }

        Piece Next(std::string_view s, size_t i)
        {
            const auto c = static_cast<unsigned char>(s[i]);

            // CSI escape: ESC [ ... final byte in 0x40..0x7E.
            if (c == 0x1B)
            {
                size_t j = i + 1;
                if (j < s.size() && s[j] == '[')
                {
                    ++j;
                    while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7E))
                        ++j;
                    if (j < s.size())
                        ++j;
                }
                return { j - i, 0 };
            }

            size_t len = 1;
            uint32_t cp = c;
            if (c >= 0xF0 && c <= 0xF4)
                len = 4, cp = c & 0x07;
            else if (c >= 0xE0)
                len = 3, cp = c & 0x0F;
            else if (c >= 0xC2 && c <= 0xDF)
                len = 2, cp = c & 0x1F;
            else if (c >= 0x80)
                return { 1, 1 };

            if (i + len > s.size())
                return { 1, 1 };
            for (size_t k = 1; k < len; k++)
            {
                const auto cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80)
                    return { 1, 1 };
                cp = (cp << 6) | (cc & 0x3F);
            }
            return { len, CodePointWidth(cp) };
        }
    }

    size_t DisplayWidth(std::string_view text)
    {
        size_t width = 0;
        for (size_t i = 0; i < text.size();)
        {
            const Piece p = Next(text, i);
            width += p.width;
            i += p.length;
        }
        return width;
    }

    std::string Fit(std::string_view text, size_t width, Align align)
    {
        std::string body;
        size_t used = DisplayWidth(text);

        if (used <= width)
        {
            body.assign(text);
        }
        else
        {
            // Keep whole pieces up to width - 3, then "...". Escapes after the
            // cut are kept so a colour that was opened gets closed again.
            const size_t keep = width >= 3 ? width - 3 : 0;
            size_t w = 0;
            bool cut = false;
            for (size_t i = 0; i < text.size();)
            {
                const Piece p = Next(text, i);
                if (p.width == 0 && p.length > 0 && text[i] == '\x1B')
                    body.append(text.substr(i, p.length));
                else if (!cut && w + p.width <= keep)
                {
                    body.append(text.substr(i, p.length));
                    w += p.width;
                }
                else
                    cut = true;
                i += p.length;
            }
            const size_t dots = std::min<size_t>(3, width);
            body.append(dots, '.');
            used = w + dots;
        }

        const size_t pad = width > used ? width - used : 0;
        size_t left = 0;
        if (align == Align::Right)
            left = pad;
        else if (align == Align::Center)
            left = pad / 2;

        return std::string(left, ' ') + body + std::string(pad - left, ' ');
    }

    Table::Table(std::vector<Column> columns) : m_columns(std::move(columns)) {}

    void Table::AddRow(std::vector<std::string> cells)
    {
        cells.resize(m_columns.size());
        m_rows.push_back(std::move(cells));
    }

    void Table::AddSeparator()
    {
        m_rows.emplace_back();
    }

    std::vector<std::string> Table::Render(const char* border, const char* reset) const
    {
        const size_t n = m_columns.size();
        std::vector<size_t> widths(n, 0);

        bool hasHeader = false;
        for (size_t c = 0; c < n; c++)
        {
            widths[c] = DisplayWidth(m_columns[c].header);
            hasHeader |= !m_columns[c].header.empty();
        }
        for (const auto& row : m_rows)
            for (size_t c = 0; c < row.size(); c++)
                widths[c] = std::max(widths[c], DisplayWidth(row[c]));
        for (size_t c = 0; c < n; c++)
            widths[c] = std::min(widths[c], std::max<size_t>(m_columns[c].maxWidth, 1));

        // Inner width: the cells, one space either side, a bar between.
        auto inner = [&] {
            size_t w = n ? n - 1 : 0;
            for (size_t c = 0; c < n; c++)
                w += widths[c] + 2;
            return w;
        };
        const size_t titleWidth = DisplayWidth(m_title);
        const size_t want = std::max(m_minWidth > 2 ? m_minWidth - 2 : 0, m_title.empty() ? 0 : titleWidth + 2);
        if (n && inner() < want)
            widths[n - 1] += want - inner();

        const std::string bar = std::string(border) + "|" + reset;

        auto rule = [&](bool joints) {
            std::string line = std::string(border) + "+";
            for (size_t c = 0; c < n; c++)
            {
                line.append(widths[c] + 2, '-');
                line += (c + 1 < n && joints) ? '+' : (c + 1 < n ? '-' : '+');
            }
            return line + reset;
        };

        auto cells = [&](const std::vector<std::string>& row, bool header) {
            std::string line = bar;
            for (size_t c = 0; c < n; c++)
            {
                const Align a = header ? Align::Center : m_columns[c].align;
                line += " " + Fit(row[c], widths[c], a) + reset + " " + bar;
            }
            return line;
        };

        std::vector<std::string> out;

        if (!m_title.empty())
        {
            out.push_back(rule(false));
            out.push_back(bar + " " + Fit(m_title, inner() - 2, Align::Center) + reset + " " + bar);
        }

        out.push_back(rule(true));
        if (hasHeader)
        {
            std::vector<std::string> header;
            for (const auto& col : m_columns)
                header.push_back(col.header);
            out.push_back(cells(header, true));
            out.push_back(rule(true));
        }

        for (const auto& row : m_rows)
            out.push_back(row.empty() ? rule(true) : cells(row, false));

        if (m_rows.empty() || !m_rows.back().empty())
            out.push_back(rule(true));

        return out;
    }

    Table KeyValue(std::string title)
    {
        Table t({ { "", Align::Right, 40 }, { "", Align::Left, 90 } });
        t.SetTitle(std::move(title));
        return t;
    }
}
