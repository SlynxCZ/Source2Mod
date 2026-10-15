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
#pragma once

#include <string>
#include <string_view>
#include <vector>

// Boxes and tables for console output. Every width here is a display width,
// not a byte count: "Aktuální mapa" is 13 columns but 15 bytes, and printf's
// %-20s pads by bytes, which is what pushes the right border out of line on
// any non-ASCII text. ANSI colour escapes count as zero, so a cell can be
// coloured and still line up -- and still line up once a client's console
// has had the escapes stripped.
namespace consoletable {
    // Columns the text takes in a monospace console.
    size_t DisplayWidth(std::string_view text);

    enum class Align { Left, Center, Right };

    // Pads (or cuts, ending in "...") to exactly `width` columns. Never splits
    // a UTF-8 sequence or an escape.
    std::string Fit(std::string_view text, size_t width, Align align = Align::Left);

    class Table
    {
    public:
        struct Column
        {
            std::string header;
            Align align = Align::Left;
            size_t maxWidth = 48;   // longer cells are cut
        };

        explicit Table(std::vector<Column> columns);

        // A line across the whole table above the headers.
        void SetTitle(std::string title) { m_title = std::move(title); }
        void AddRow(std::vector<std::string> cells);
        // A rule between the rows added before and after it.
        void AddSeparator();
        // Makes the table at least this wide (the last column takes the rest).
        void SetMinWidth(size_t width) { m_minWidth = width; }

        // `border` colours the frame (an escape, or "" for none).
        std::vector<std::string> Render(const char* border = "", const char* reset = "") const;

    private:
        std::vector<Column> m_columns;
        std::vector<std::vector<std::string>> m_rows;   // empty = separator
        std::string m_title;
        size_t m_minWidth = 0;
    };

    // A two-column key/value table without a header row.
    Table KeyValue(std::string title);
}
