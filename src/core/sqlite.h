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

/*
 * The SQLite driver of ISource2ModSQL: an SQLite file behind the same
 * connection / query / transaction / serverless calls as MySQL. Like the
 * MySQL connection, each connection runs its statements on a thread of its
 * own and hands results back on the main thread (RunFrame); results are read
 * into the same stored result sets, so ISource2ModSQLResult behaves alike.
 *
 * The approach follows zer0k-z/sql_mm's SQLite connector (GPL-3.0); the
 * code is Source2Mod's own.
 */
#pragma once

#include "mysql.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct sqlite3;

namespace mysql
{
    /// The file an SQLite connection opens: absolute as given, else under
    /// addons/source2mod/data (made when missing).
    std::string SqlitePath(const std::string& database);

    /// Opens the file; nullptr (and why) when it cannot be.
    sqlite3* SqliteOpen(const OwnedConnectionInfo& info, std::string* error, unsigned int* errorCode);

    /// Runs every statement of @p sql, reading the rows of each into @p sets.
    bool SqliteRun(sqlite3* db, const std::string& sql, std::vector<MySQLResultSet>* sets, unsigned int* insertId,
                   unsigned int* affectedRows, std::string* error, unsigned int* errorCode);

    /// Serverless placeholders for SQLite: `?` a quoted value, `??` a quoted identifier.
    bool SqliteFormatQuery(const std::string& query, const std::vector<std::string>& params, std::string* out, std::string* error);

    /// '' for every ' (what goes between single quotes).
    std::string SqliteEscape(const char* text);

    class SQLiteConnection : public ISource2ModSQLConnection
    {
    public:
        SQLiteConnection(PluginId owner, const Source2ModSQLConnectionInfo& info);
        ~SQLiteConnection() override;

        PluginId Owner() const { return m_owner; }

        void Connect(Source2ModSQLConnectCallbackFunc callback) override;
        void Query(char* query, Source2ModSQLQueryCallbackFunc callback) override;
        void Query(const char* query, Source2ModSQLQueryCallbackFunc callback, ...) override;
        void ExecuteTransaction(Source2ModSQLTransaction txn, Source2ModSQLTransactionSuccessCallbackFunc success,
                                Source2ModSQLTransactionFailureCallbackFunc failure) override;
        void Destroy() override;
        ESource2ModSQLDriver GetDriver() override { return SOURCE2MOD_SQL_DRIVER_SQLITE; }
        std::string Escape(char* string) override { return SqliteEscape(string); }
        std::string Escape(const char* string) override { return SqliteEscape(string); }

        /// Callbacks of finished work, on the main thread.
        void RunFrame();

    private:
        using Task = std::function<void()>;

        /// Work for the connection's thread; the task it returns runs on the main thread.
        void Post(std::function<Task()> work);
        void ThreadRun();

        OwnedConnectionInfo m_info;
        PluginId m_owner;
        sqlite3* m_db = nullptr;     // only touched on the connection's thread

        std::queue<std::function<Task()>> m_work;
        std::queue<Task> m_done;
        std::mutex m_workLock;
        std::mutex m_doneLock;
        std::condition_variable m_wake;
        bool m_terminate = false;
        std::thread m_thread;
    };
}
