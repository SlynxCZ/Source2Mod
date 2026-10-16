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
 * The SQLite driver of ISource2ModSQL. See sqlite.h.
 */
#include "sqlite.h"

#include "paths.h"
#include "utils/log.h"

#include <sqlite3.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>

namespace mysql
{
    std::string SqlitePath(const std::string& database)
    {
        std::filesystem::path path(database.empty() ? std::string("source2mod.sq3") : database);
        if (!path.is_absolute())
            path = std::filesystem::path(paths::pathsManager.RootDirectory()) / "data" / path;

        // SourceMod's habit: a bare name gets the .sq3 extension.
        if (!path.has_extension())
            path += ".sq3";

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        return path.string();
    }

    sqlite3* SqliteOpen(const OwnedConnectionInfo& info, std::string* error, unsigned int* errorCode)
    {
        const std::string path = SqlitePath(info.m_sDatabase);

        sqlite3* db = nullptr;
        const int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        if (rc != SQLITE_OK)
        {
            if (error)
                *error = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
            if (errorCode)
                *errorCode = (unsigned int)rc;
            sqlite3_close(db);
            return nullptr;
        }

        // Another connection (or the serverless worker) on the same file
        // waits for it instead of failing with SQLITE_BUSY at once.
        sqlite3_busy_timeout(db, (info.m_nTimeout > 0 ? info.m_nTimeout : 60) * 1000);
        return db;
    }

    namespace
    {
        int SqliteType(int type)
        {
            switch (type)
            {
                case SQLITE_INTEGER: return SOURCE2MOD_SQL_TYPE_LONGLONG;
                case SQLITE_FLOAT: return SOURCE2MOD_SQL_TYPE_DOUBLE;
                case SQLITE_BLOB: return SOURCE2MOD_SQL_TYPE_BLOB;
                case SQLITE_NULL: return SOURCE2MOD_SQL_TYPE_NULL;
                default: return SOURCE2MOD_SQL_TYPE_STRING;
            }
        }
    }

    bool SqliteRun(sqlite3* db, const std::string& sql, std::vector<MySQLResultSet>* sets, unsigned int* insertId,
                   unsigned int* affectedRows, std::string* error, unsigned int* errorCode)
    {
        const char* tail = sql.c_str();
        const char* end = tail + sql.size();
        int changes = 0;

        while (tail < end)
        {
            sqlite3_stmt* stmt = nullptr;
            const int prc = sqlite3_prepare_v2(db, tail, (int)(end - tail), &stmt, &tail);
            if (prc != SQLITE_OK)
            {
                if (error)
                    *error = sqlite3_errmsg(db);
                if (errorCode)
                    *errorCode = (unsigned int)prc;
                return false;
            }

            // Whitespace or a comment after the last ';'.
            if (!stmt)
                continue;

            MySQLResultSet set;
            const int columns = sqlite3_column_count(stmt);
            set.m_vecFields.resize(columns);
            for (int i = 0; i < columns; i++)
            {
                const char* name = sqlite3_column_name(stmt, i);
                set.m_vecFields[i].m_sName = name ? name : "";
            }

            int rc;
            bool typed = false;
            while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
            {
                std::vector<MySQLResultSet::Cell> row(columns);
                for (int i = 0; i < columns; i++)
                {
                    const int type = sqlite3_column_type(stmt, i);

                    // SQLite types values, not columns: the first row decides,
                    // as far as the field is anything but NULL.
                    if (!typed || set.m_vecFields[i].m_nType == SOURCE2MOD_SQL_TYPE_NULL)
                        set.m_vecFields[i].m_nType = SqliteType(type);

                    if (type == SQLITE_NULL)
                        continue;

                    row[i].m_bNull = false;
                    if (type == SQLITE_BLOB)
                    {
                        const void* data = sqlite3_column_blob(stmt, i);
                        row[i].m_sData.assign((const char*)data, (size_t)sqlite3_column_bytes(stmt, i));
                    }
                    else
                    {
                        // Numbers as their text, which is what the result's
                        // GetInt / GetFloat read, as with MySQL.
                        const unsigned char* text = sqlite3_column_text(stmt, i);
                        row[i].m_sData.assign((const char*)text, (size_t)sqlite3_column_bytes(stmt, i));
                    }
                }
                typed = true;
                set.m_vecRows.push_back(std::move(row));
            }

            if (rc != SQLITE_DONE)
            {
                if (error)
                    *error = sqlite3_errmsg(db);
                if (errorCode)
                    *errorCode = (unsigned int)rc;
                sqlite3_finalize(stmt);
                return false;
            }

            if (columns == 0)
                changes = sqlite3_changes(db);
            else if (sets)
            {
                for (auto& field : set.m_vecFields)
                {
                    if (!typed)
                        field.m_nType = SOURCE2MOD_SQL_TYPE_STRING;
                }
                sets->push_back(std::move(set));
            }

            sqlite3_finalize(stmt);
        }

        if (insertId)
            *insertId = (unsigned int)sqlite3_last_insert_rowid(db);
        if (affectedRows)
            *affectedRows = (unsigned int)changes;
        return true;
    }

    std::string SqliteEscape(const char* text)
    {
        std::string out;
        if (!text)
            return out;

        for (const char* c = text; *c; c++)
        {
            if (*c == '\'')
                out.push_back('\'');
            out.push_back(*c);
        }
        return out;
    }

    bool SqliteFormatQuery(const std::string& query, const std::vector<std::string>& params, std::string* out, std::string* error)
    {
        out->clear();
        out->reserve(query.size() + params.size() * 16);

        size_t next = 0;
        char quote = '\0';

        for (size_t i = 0; i < query.size(); i++)
        {
            const char c = query[i];

            // Inside a literal a ? is a question mark; SQLite doubles quotes
            // and has no backslash escapes.
            if (quote != '\0')
            {
                out->push_back(c);
                if (c == quote)
                    quote = '\0';
                continue;
            }

            if (c == '\'' || c == '"' || c == '`')
            {
                quote = c;
                out->push_back(c);
                continue;
            }

            if (c != '?')
            {
                out->push_back(c);
                continue;
            }

            const bool identifier = (i + 1 < query.size() && query[i + 1] == '?');
            if (identifier)
                i++;

            if (next >= params.size())
            {
                *error = "Serverless query has more placeholders than parameters";
                return false;
            }

            const std::string& value = params[next++];
            const char q = identifier ? '"' : '\'';

            out->push_back(q);
            for (const char v : value)
            {
                if (v == q)
                    out->push_back(q);
                out->push_back(v);
            }
            out->push_back(q);
        }

        if (quote != '\0')
        {
            *error = "Serverless query has an unterminated string literal";
            return false;
        }

        if (next != params.size())
        {
            *error = "Serverless query has more parameters than placeholders";
            return false;
        }

        return true;
    }

    SQLiteConnection::SQLiteConnection(PluginId owner, const Source2ModSQLConnectionInfo& info)
        : m_info(info), m_owner(owner)
    {
        m_info.m_eDriver = SOURCE2MOD_SQL_DRIVER_SQLITE;
    }

    SQLiteConnection::~SQLiteConnection()
    {
        if (m_thread.joinable())
        {
            {
                std::lock_guard<std::mutex> lock(m_workLock);
                m_terminate = true;
            }
            m_wake.notify_all();
            m_thread.join();
        }

        // Not run, but destroyed here on the main thread while the plugin
        // whose callbacks these hold is still mapped.
        std::queue<std::function<Task()>>().swap(m_work);
        std::queue<Task>().swap(m_done);

        if (m_db)
            sqlite3_close(m_db);
    }

    void SQLiteConnection::Post(std::function<Task()> work)
    {
        std::lock_guard<std::mutex> lock(m_workLock);
        if (!m_thread.joinable())
            m_thread = std::thread(&SQLiteConnection::ThreadRun, this);

        m_work.push(std::move(work));
        m_wake.notify_one();
    }

    void SQLiteConnection::ThreadRun()
    {
        std::unique_lock<std::mutex> lock(m_workLock);

        while (true)
        {
            if (m_work.empty())
            {
                if (m_terminate)
                    return;

                m_wake.wait(lock);
                continue;
            }

            auto work = std::move(m_work.front());
            m_work.pop();

            lock.unlock();
            Task done = work();
            {
                std::lock_guard<std::mutex> doneLock(m_doneLock);
                m_done.push(std::move(done));
            }
            // The work item holds the plugin's callback too; it goes on the
            // main thread, inside the task.
            work = nullptr;
            lock.lock();
        }
    }

    void SQLiteConnection::RunFrame()
    {
        Task task;
        {
            std::lock_guard<std::mutex> lock(m_doneLock);
            if (m_done.empty())
                return;

            task = std::move(m_done.front());
            m_done.pop();
        }

        // May Destroy() this connection; nothing of it is touched after.
        task();
    }

    void SQLiteConnection::Connect(Source2ModSQLConnectCallbackFunc callback)
    {
        auto cb = std::make_shared<Source2ModSQLConnectCallbackFunc>(std::move(callback));
        Post([this, cb]() -> Task {
            bool ok = m_db != nullptr;
            std::string error;
            if (!ok)
            {
                m_db = SqliteOpen(m_info, &error, nullptr);
                ok = m_db != nullptr;
            }

            return [cb, ok, error, database = m_info.m_sDatabase]() {
                if (!ok)
                    FP_ERROR("SQLite: could not open '{}': {}", database, error);
                if (*cb)
                    (*cb)(ok);
            };
        });
    }

    void SQLiteConnection::Query(char* query, Source2ModSQLQueryCallbackFunc callback)
    {
        auto cb = std::make_shared<Source2ModSQLQueryCallbackFunc>(std::move(callback));
        Post([this, cb, sql = std::string(query ? query : "")]() -> Task {
            std::string error;
            std::vector<MySQLResultSet> sets;
            unsigned int insertId = 0, affected = 0;

            bool ok = false;
            if (!m_db)
                error = "not connected";
            else
                ok = SqliteRun(m_db, sql, &sets, &insertId, &affected, &error, nullptr);

            auto result = std::make_shared<CMySQLStoredQuery>(std::move(sets), insertId, affected);
            return [cb, ok, error, result]() {
                if (!ok)
                    FP_ERROR("SQLite query error: {}", error);
                if (*cb)
                    (*cb)(ok ? result.get() : nullptr);
            };
        });
    }

    void SQLiteConnection::Query(const char* query, Source2ModSQLQueryCallbackFunc callback, ...)
    {
        va_list args;
        va_start(args, callback);

        va_list vaCopy;
        va_copy(vaCopy, args);
        const int len = std::vsnprintf(nullptr, 0, query, vaCopy);
        va_end(vaCopy);

        std::vector<char> buffer(len > 0 ? len + 1 : 1);
        std::vsnprintf(buffer.data(), buffer.size(), query, args);
        va_end(args);

        Query(buffer.data(), std::move(callback));
    }

    void SQLiteConnection::ExecuteTransaction(Source2ModSQLTransaction txn, Source2ModSQLTransactionSuccessCallbackFunc success,
                                              Source2ModSQLTransactionFailureCallbackFunc failure)
    {
        auto ok = std::make_shared<Source2ModSQLTransactionSuccessCallbackFunc>(std::move(success));
        auto fail = std::make_shared<Source2ModSQLTransactionFailureCallbackFunc>(std::move(failure));

        Post([this, ok, fail, queries = std::move(txn.queries)]() -> Task {
            std::string error;
            int failIndex = -1;
            auto results = std::make_shared<std::vector<std::unique_ptr<CMySQLStoredQuery>>>();

            auto simple = [this](const char* sql) { return sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; };

            if (!m_db)
                error = "not connected";
            else if (!simple("BEGIN"))
                error = std::string("SQLite query error: ") + sqlite3_errmsg(m_db);
            else
            {
                for (size_t i = 0; i < queries.size(); i++)
                {
                    std::vector<MySQLResultSet> sets;
                    unsigned int insertId = 0, affected = 0;
                    std::string e;
                    if (!SqliteRun(m_db, queries[i], &sets, &insertId, &affected, &e, nullptr))
                    {
                        error = "SQLite query error: " + e;
                        failIndex = (int)i;
                        break;
                    }
                    results->push_back(std::make_unique<CMySQLStoredQuery>(std::move(sets), insertId, affected));
                }

                if (error.empty() && !simple("COMMIT"))
                    error = std::string("SQLite query error: ") + sqlite3_errmsg(m_db);
                if (!error.empty())
                    simple("ROLLBACK");
            }

            return [ok, fail, error, failIndex, results]() {
                if (error.empty())
                {
                    std::vector<ISource2ModSQLQuery*> queries;
                    for (auto& q : *results)
                        queries.push_back(q.get());
                    if (*ok)
                        (*ok)(queries);
                    return;
                }

                FP_ERROR("SQLite transaction error: {}", error);
                if (*fail)
                    (*fail)(error, failIndex);
            };
        });
    }

    void SQLiteConnection::Destroy()
    {
        std::erase(mysqlManager.m_vecSqliteConnections, this);
        delete this;
    }
}
