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
#pragma once

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <memory>

class log {
public:
    static void Init();
    static void Close();

    static std::shared_ptr<spdlog::logger>& GetLogger() { return m_FP_logger; }
    // Source2Mod.log only: for lines that reach the console some other way
    // (a SourcePawn LogMessage goes there like a C++ plugin's Log does).
    static std::shared_ptr<spdlog::logger>& GetFileLogger() { return m_FP_fileLogger; }

private:
    static std::shared_ptr<spdlog::logger> m_FP_logger;
    static std::shared_ptr<spdlog::logger> m_FP_fileLogger;
};

#define FP_TRACE(fmt, ...)    ::log::GetLogger()->trace(fmt, ##__VA_ARGS__)
#define FP_DEBUG(fmt, ...)    ::log::GetLogger()->debug(fmt, ##__VA_ARGS__)
#define FP_INFO(fmt, ...)     ::log::GetLogger()->info(fmt, ##__VA_ARGS__)
#define FP_WARN(fmt, ...)     ::log::GetLogger()->warn(fmt, ##__VA_ARGS__)
#define FP_ERROR(fmt, ...)    ::log::GetLogger()->error(fmt, ##__VA_ARGS__)
#define FP_CRITICAL(fmt, ...) ::log::GetLogger()->critical(fmt, ##__VA_ARGS__)
