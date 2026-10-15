/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
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
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#include "log.h"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#if defined(_WIN32)
#include <windows.h>
#endif

std::shared_ptr<spdlog::logger> log::m_FP_logger;
std::shared_ptr<spdlog::logger> log::m_FP_fileLogger;

void log::Init() {
#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_ERROR_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode))
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

    std::vector<spdlog::sink_ptr> sinks;

    auto color_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    color_sink->set_pattern("%^[%T.%e] %n: %v%$");

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Source2Mod.log", true);
    file_sink->set_pattern("[%T.%e] [%^%l%$] %n: %v");

    sinks.emplace_back(color_sink);
    sinks.emplace_back(file_sink);

    m_FP_logger = std::make_shared<spdlog::logger>("Source2Mod", sinks.begin(), sinks.end());
    register_logger(m_FP_logger);
    m_FP_logger->set_level(spdlog::level::trace);
    m_FP_logger->flush_on(spdlog::level::info);

    // Same file, same name in it, not registered (the name is taken).
    m_FP_fileLogger = std::make_shared<spdlog::logger>("Source2Mod", file_sink);
    m_FP_fileLogger->set_level(spdlog::level::trace);
    m_FP_fileLogger->flush_on(spdlog::level::info);

    spdlog::cfg::load_env_levels();
}

void log::Close() {
    spdlog::drop("Source2Mod");
    m_FP_logger.reset();
    m_FP_fileLogger.reset();
}
