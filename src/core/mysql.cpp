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
#include "mysql.h"

#include "pluginapi.h"
#include "utils/log.h"

#include <algorithm>

namespace mysql
{
    MySQLManager mysqlManager;

    namespace
    {
        /// Opens one connection from a set of details. Runs on a worker
        /// thread, never on the main one.
        ///
        /// Null on failure, with the reason written out first: mysql_error()
        /// reads out of the handle, so it has nothing left to say once that
        /// handle has been closed.
        MYSQL *OpenConnection(const OwnedConnectionInfo &info, std::string *error, unsigned int *errorCode)
        {
            MYSQL *mysql = mysql_init(NULL);
            if (!mysql)
            {
                if (error)
                    *error = "Out of memory allocating a MySQL handle";
                return nullptr;
            }

            const unsigned int timeout = info.m_nTimeout > 0 ? (unsigned int)info.m_nTimeout : 60;
            mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout);
            mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, (const char *)&timeout);
            mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, (const char *)&timeout);

            bool my_true = true;
            mysql_options(mysql, MYSQL_OPT_RECONNECT, (const char *)&my_true); // deprecated

            const char *host = info.m_sHost.c_str();
            const char *socket = NULL;

            // A host that is a path is a unix socket to connect over. The
            // path is what has to be handed to libmysql as the socket -- this
            // used to pass "localhost" as both, which is not a path at all.
            if (!info.m_sHost.empty() && info.m_sHost[0] == '/')
            {
                socket = info.m_sHost.c_str();
                host = "localhost";
            }

            if (!mysql_real_connect(mysql, host, info.m_sUser.c_str(), info.m_sPass.c_str(),
                                    info.m_sDatabase.c_str(), info.m_nPort, socket, ((1) << 17)))
            {
                if (error)
                    *error = mysql_error(mysql);
                if (errorCode)
                    *errorCode = mysql_errno(mysql);

                mysql_close(mysql);
                return nullptr;
            }

            return mysql;
        }

        /// Copies a result set out of libmysql, rows and all, so it can be
        /// read after the connection it came off is gone.
        MySQLResultSet StoreResult(MYSQL_RES *res)
        {
            MySQLResultSet set;

            const unsigned int fields = mysql_num_fields(res);
            set.m_vecFields.reserve(fields);

            for (unsigned int i = 0; i < fields; i++)
            {
                MYSQL_FIELD *field = mysql_fetch_field_direct(res, i);

                MySQLResultSet::Field out;
                out.m_sName = (field && field->name) ? field->name : "";
                out.m_nType = field ? (int)field->type : SOURCE2MOD_MYSQL_TYPE_UNKNOWN;

                set.m_vecFields.push_back(std::move(out));
            }

            set.m_vecRows.reserve((size_t)mysql_num_rows(res));

            while (MYSQL_ROW row = mysql_fetch_row(res))
            {
                unsigned long *lengths = mysql_fetch_lengths(res);

                std::vector<MySQLResultSet::Cell> cells;
                cells.reserve(fields);

                for (unsigned int i = 0; i < fields; i++)
                {
                    MySQLResultSet::Cell cell;
                    cell.m_bNull = row[i] == NULL;

                    // By length, not by NUL: a BLOB column is allowed to have
                    // one in the middle of it.
                    if (!cell.m_bNull)
                        cell.m_sData.assign(row[i], lengths ? lengths[i] : strlen(row[i]));

                    cells.push_back(std::move(cell));
                }

                set.m_vecRows.push_back(std::move(cells));
            }

            return set;
        }

        /// Reads everything a query that just ran left on the connection:
        /// every result set, rows and all, plus the insert id and affected
        /// rows of the statement that was asked for. Runs on the worker right
        /// after mysql_query(), so the connection is left with nothing pending
        /// -- a stored procedure's trailing results included -- and the main
        /// thread never has to go near it to read or free a result.
        ///
        /// On failure the sets are cleared and the error written out.
        bool ReadResults(MYSQL *mysql, std::vector<MySQLResultSet> *sets, unsigned int *insertId,
                         unsigned int *affectedRows, std::string *error, unsigned int *errorCode)
        {
            bool first = true;

            while (true)
            {
                bool failed = false;

                if (MYSQL_RES *res = mysql_store_result(mysql))
                {
                    sets->push_back(StoreResult(res));
                    mysql_free_result(res);
                }
                else if (mysql_field_count(mysql) != 0)
                {
                    failed = true;
                }

                // Both belong to the statement that was asked for, so they are
                // read off the first result and not whatever a stored procedure
                // went on to do. Not before it, either: a SELECT only reports its
                // affected rows once its result set has been stored.
                if (!failed && first)
                {
                    *insertId = (unsigned int)mysql_insert_id(mysql);
                    *affectedRows = (unsigned int)mysql_affected_rows(mysql);
                    first = false;
                }

                // 0 is another result to read, -1 none left, above 0 a later
                // statement of the same query failed.
                if (!failed)
                {
                    if (!mysql_more_results(mysql))
                        return true;

                    failed = mysql_next_result(mysql) > 0;
                }

                if (failed)
                {
                    if (error)
                        *error = mysql_error(mysql);
                    if (errorCode)
                        *errorCode = mysql_errno(mysql);

                    sets->clear();
                    return false;
                }
            }
        }

        /// Fills a query's placeholders in against the live connection: `?`
        /// becomes the escaped value in quotes, `??` a quoted identifier.
        ///
        /// Escaping is why this happens here rather than where the query was
        /// written: mysql_real_escape_string() needs a connection to know the
        /// character set and whether backslashes escape anything, and a
        /// serverless query has none until the worker opens one.
        bool FormatQuery(MYSQL *mysql, const std::string &query, const std::vector<std::string> &params,
                         std::string *out, std::string *error)
        {
            out->clear();
            out->reserve(query.size() + params.size() * 16);

            size_t next = 0;
            char quote = '\0';

            for (size_t i = 0; i < query.size(); i++)
            {
                const char c = query[i];

                // Inside a literal, a ? is just a question mark.
                if (quote != '\0')
                {
                    out->push_back(c);

                    if (c == '\\' && quote != '`' && i + 1 < query.size())
                    {
                        out->push_back(query[++i]);
                        continue;
                    }

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

                const std::string &value = params[next++];

                if (identifier)
                {
                    out->push_back('`');
                    for (const char id : value)
                    {
                        if (id == '`')
                            out->push_back('`');
                        out->push_back(id);
                    }
                    out->push_back('`');
                    continue;
                }

                std::vector<char> buffer(value.size() * 2 + 1);
                const unsigned long length = mysql_real_escape_string(mysql, buffer.data(), value.c_str(),
                                                                      (unsigned long)value.size());

                out->push_back('\'');
                out->append(buffer.data(), length);
                out->push_back('\'');
            }

            if (quote != '\0')
            {
                *error = "Serverless query has an unterminated string literal";
                return false;
            }

            if (next != params.size())
            {
                *error = "Serverless query has fewer placeholders than parameters";
                return false;
            }

            return true;
        }
    }

    void TMySQLConnectOp::RunThreadPart()
    {
        m_szError[0] = '\0';

        std::string error;
        m_pDatabase = OpenConnection(m_pCon->m_info, &error, nullptr);

        if (!m_pDatabase)
        {
            V_snprintf(m_szError, sizeof m_szError, "%s", error.c_str());
        }
    }

    void TMySQLConnectOp::RunThinkPart()
    {
        if (m_szError[0])
        {
            FP_ERROR("Failed to establish a MySQL connection: {}", m_szError);
        }

        m_pCon->SetDatabase(m_pDatabase);
        m_callback(m_pDatabase != nullptr);
    }

    void TMySQLConnectOp::CancelThinkPart()
    {
        // Only the handle this op opened, which was never installed. The
        // connection's own one is closed by its destructor.
        mysql_close(m_pDatabase);
        m_pDatabase = nullptr;
    }

    TMySQLQueryOp::~TMySQLQueryOp()
    {
        delete m_pQuery;
    }

    void TMySQLQueryOp::Fail(const char *error)
    {
        V_snprintf(m_szError, sizeof m_szError, "MySQL query error: %s", error);
    }

    void TMySQLQueryOp::RunThreadPart()
    {
        auto pDatabase = m_pCon->GetDatabase();
        m_szError[0] = '\0';
        if (!pDatabase)
        {
            Fail("not connected");
            return;
        }

        if (mysql_query(pDatabase, m_szQuery.c_str()))
        {
            Fail(mysql_error(pDatabase));
            return;
        }

        // Everything is read off here, on the worker: by the time the callback
        // runs, the worker may already be on the next query of this connection.
        std::vector<MySQLResultSet> sets;
        unsigned int insertId = 0, affectedRows = 0;
        std::string error;

        if (!ReadResults(pDatabase, &sets, &insertId, &affectedRows, &error, nullptr))
        {
            Fail(error.c_str());
            return;
        }

        m_pQuery = new CMySQLStoredQuery(std::move(sets), insertId, affectedRows);
    }

    void TMySQLQueryOp::RunThinkPart()
    {
        if (m_szError[0])
        {
            FP_ERROR("{}", m_szError);

            // A null query is how a failure reaches the plugin -- the same
            // signal a serverless result gives with a null m_pQuery. Without
            // it the plugin would wait on this callback forever.
            if (m_callback)
                m_callback(nullptr);
            return;
        }

        // Owned by this op and freed with it, once the callback has returned.
        if (m_callback)
            m_callback(m_pQuery);
    }

    void TMySQLQueryOp::CancelThinkPart()
    {
        // Nothing to do: the result is freed with the op, and the connection
        // closed by its destructor once every op is gone.
    }

    TMySQLTransactOp::~TMySQLTransactOp()
    {
        for (auto query : m_pQueries)
        {
            delete query;
        }
        m_pQueries.clear();
    }

    void TMySQLTransactOp::Fail(const char *error)
    {
        V_snprintf(m_szError, sizeof m_szError, "MySQL query error: %s", error);
        m_failIndex = -1;
    }

    void TMySQLTransactOp::RunThreadPart()
    {
        if (!m_pCon->GetDatabase())
        {
            Fail("not connected");
            return;
        }

        if (!this->DoSimpleQuery("BEGIN"))
        {
            return;
        }
        for (size_t i = 0; i < this->m_txn.queries.size(); i++)
        {
            CMySQLStoredQuery *result = DoQuery(this->m_txn.queries[i].c_str());
            if (!result)
            {
                Rollback();
                m_failIndex = (int)i;
                return;
            }
            this->m_pQueries.push_back(result);
        }
        if (!this->DoSimpleQuery("COMMIT"))
        {
            Rollback();
            return;
        }
    }

    void TMySQLTransactOp::CancelThinkPart()
    {
        // See TMySQLQueryOp::CancelThinkPart().
    }

    void TMySQLTransactOp::RunThinkPart()
    {
        if (!m_szError[0])
        {
            // The queries stay this op's and are freed with it once the
            // callback returns, the same as a single query's result.
            if (m_successCallback)
                m_successCallback(m_pQueries);
            return;
        }
        FP_ERROR("TMySQLTransactOp ERROR: {}", m_szError);
        if (m_failureCallback)
            m_failureCallback(m_szError, m_failIndex);
    }

    void TMySQLTransactOp::Rollback()
    {
        // Straight to the connection: a ROLLBACK that fails as well must not
        // overwrite the error that made it necessary.
        auto pDatabase = m_pCon->GetDatabase();
        if (pDatabase && mysql_query(pDatabase, "ROLLBACK") == 0)
        {
            std::vector<MySQLResultSet> sets;
            unsigned int insertId = 0, affectedRows = 0;
            ReadResults(pDatabase, &sets, &insertId, &affectedRows, nullptr, nullptr);
        }
    }

    bool TMySQLTransactOp::DoSimpleQuery(const char *query)
    {
        auto pQuery = DoQuery(query);
        if (!pQuery)
        {
            return false;
        }
        delete pQuery;
        return true;
    }

    CMySQLStoredQuery *TMySQLTransactOp::DoQuery(const char *query)
    {
        auto pDatabase = m_pCon->GetDatabase();
        if (mysql_query(pDatabase, query))
        {
            V_snprintf(m_szError, sizeof m_szError, "MySQL query error: %s", mysql_error(pDatabase));
            return NULL;
        }

        std::vector<MySQLResultSet> sets;
        unsigned int insertId = 0, affectedRows = 0;
        std::string error;

        if (!ReadResults(pDatabase, &sets, &insertId, &affectedRows, &error, nullptr))
        {
            // Has to say so: an empty error reads as success, and the
            // plugin would be handed a transaction that was rolled back.
            V_snprintf(m_szError, sizeof m_szError, "MySQL query error: %s", error.c_str());
            return NULL;
        }

        return new CMySQLStoredQuery(std::move(sets), insertId, affectedRows);
    }

    void CMySQLStoredResult::Reset(const MySQLResultSet *set)
    {
        m_pSet = set;
        m_CurRow = 0;
    }

    const MySQLResultSet::Cell *CMySQLStoredResult::Cell(unsigned int columnId) const
    {
        if (!m_pSet || columnId >= m_pSet->m_vecFields.size())
        {
            return nullptr;
        }

        // Nothing fetched yet, or fetched one past the end.
        if (!m_CurRow || m_CurRow > m_pSet->m_vecRows.size())
        {
            return nullptr;
        }

        return &m_pSet->m_vecRows[m_CurRow - 1][columnId];
    }

    int CMySQLStoredResult::GetRowCount()
    {
        return m_pSet ? (int)m_pSet->m_vecRows.size() : 0;
    }

    int CMySQLStoredResult::GetFieldCount()
    {
        return m_pSet ? (int)m_pSet->m_vecFields.size() : 0;
    }

    bool CMySQLStoredResult::FieldNameToNum(const char *name, unsigned int *columnId)
    {
        if (!m_pSet || !name)
        {
            return false;
        }

        for (size_t i = 0; i < m_pSet->m_vecFields.size(); i++)
        {
            if (m_pSet->m_vecFields[i].m_sName == name)
            {
                *columnId = (unsigned int)i;
                return true;
            }
        }

        return false;
    }

    const char *CMySQLStoredResult::FieldNumToName(unsigned int colId)
    {
        if (!m_pSet || colId >= m_pSet->m_vecFields.size())
        {
            return NULL;
        }

        return m_pSet->m_vecFields[colId].m_sName.c_str();
    }

    bool CMySQLStoredResult::MoreRows()
    {
        return m_pSet && m_CurRow < m_pSet->m_vecRows.size();
    }

    ISource2ModMySQLRow *CMySQLStoredResult::FetchRow()
    {
        if (!m_pSet || m_CurRow >= m_pSet->m_vecRows.size())
        {
            /* Put us one after so we know to block CurrentRow() */
            m_CurRow = m_pSet ? (unsigned int)m_pSet->m_vecRows.size() + 1 : 1;
            return NULL;
        }

        m_CurRow++;
        return this;
    }

    ISource2ModMySQLRow *CMySQLStoredResult::CurrentRow()
    {
        if (!m_pSet || !m_CurRow || m_CurRow > m_pSet->m_vecRows.size())
        {
            return NULL;
        }

        return this;
    }

    bool CMySQLStoredResult::Rewind()
    {
        m_CurRow = 0;
        return true;
    }

    int CMySQLStoredResult::GetFieldType(unsigned int field)
    {
        if (!m_pSet || field >= m_pSet->m_vecFields.size())
        {
            return SOURCE2MOD_MYSQL_TYPE_UNKNOWN;
        }

        return m_pSet->m_vecFields[field].m_nType;
    }

    bool CMySQLStoredResult::IsNull(unsigned int columnId)
    {
        const auto *cell = Cell(columnId);
        return !cell || cell->m_bNull;
    }

    const char *CMySQLStoredResult::GetString(unsigned int columnId, size_t *length)
    {
        const auto *cell = Cell(columnId);

        if (!cell || cell->m_bNull)
        {
            if (length)
            {
                *length = 0;
            }
            return nullptr;
        }

        if (length)
        {
            *length = cell->m_sData.size();
        }

        return cell->m_sData.c_str();
    }

    size_t CMySQLStoredResult::GetDataSize(unsigned int columnId)
    {
        const auto *cell = Cell(columnId);
        return (cell && !cell->m_bNull) ? cell->m_sData.size() : 0;
    }

    float CMySQLStoredResult::GetFloat(unsigned int columnId)
    {
        const auto *cell = Cell(columnId);
        return (cell && !cell->m_bNull) ? (float)atof(cell->m_sData.c_str()) : 0.0f;
    }

    int CMySQLStoredResult::GetInt(unsigned int columnId)
    {
        const auto *cell = Cell(columnId);
        return (cell && !cell->m_bNull) ? atoi(cell->m_sData.c_str()) : 0;
    }

    int64_t CMySQLStoredResult::GetInt64(unsigned int columnId)
    {
        const auto *cell = Cell(columnId);
        return (cell && !cell->m_bNull) ? atoll(cell->m_sData.c_str()) : 0;
    }

    CMySQLStoredQuery::CMySQLStoredQuery(std::vector<MySQLResultSet> sets, unsigned int insertId, unsigned int affectedRows)
        : m_vecSets(std::move(sets)), m_res(nullptr), m_insertId(insertId), m_affectedRows(affectedRows)
    {
        if (!m_vecSets.empty())
        {
            m_res.Reset(&m_vecSets[0]);
        }
    }

    ISource2ModMySQLResult *CMySQLStoredQuery::GetResultSet()
    {
        // A statement that returns no rows at all -- an INSERT, an UPDATE --
        // has no result set to hand back.
        if (m_CurSet >= m_vecSets.size())
        {
            return NULL;
        }

        return &m_res;
    }

    bool CMySQLStoredQuery::FetchMoreResults()
    {
        if (m_CurSet + 1 >= m_vecSets.size())
        {
            return false;
        }

        m_CurSet++;
        m_res.Reset(&m_vecSets[m_CurSet]);
        return true;
    }

    unsigned int CMySQLStoredQuery::GetInsertId()
    {
        return m_insertId;
    }

    unsigned int CMySQLStoredQuery::GetAffectedRows()
    {
        return m_affectedRows;
    }

    MySQLConnection::MySQLConnection(PluginId owner, const Source2ModMySQLConnectionInfo info)
    {
        this->m_info = OwnedConnectionInfo(info);
        this->m_Owner = owner;
    }

    MySQLConnection::~MySQLConnection()
    {
        FP_DEBUG("Destroying MySQL connection {}", m_info.m_sDatabase);
        if (m_thread)
        {
            {
                std::lock_guard<std::mutex> lock(m_Lock);
                m_Terminate = true;
                m_QueueEvent.notify_all();
            }

            m_thread->join();
            m_thread.reset();
            m_Terminate = false;
        }

        while (!m_ThinkQueue.empty())
        {
            ThreadOperation *op = m_ThinkQueue.front();
            m_ThinkQueue.pop();

            op->CancelThinkPart();
            delete op;
        }

        // What the worker never reached. These used to be dropped on the
        // floor, which left their callbacks -- std::functions holding code
        // inside the owning plugin -- alive with nothing to destroy them.
        while (!m_threadQueue.empty())
        {
            delete m_threadQueue.front();
            m_threadQueue.pop();
        }

        if (m_pDatabase)
        {
            mysql_close(m_pDatabase);
        }
    }

    void MySQLConnection::Connect(Source2ModMySQLConnectCallbackFunc callback)
    {
        TMySQLConnectOp *op = new TMySQLConnectOp(this, callback);

        AddToThreadQueue(op);
    }

    void MySQLConnection::Query(char *query, Source2ModMySQLQueryCallbackFunc callback)
    {
        TMySQLQueryOp *op = new TMySQLQueryOp(this, std::string(query), callback);

        if (!m_pDatabase)
        {
            // Still answered, a frame later on the main thread like any other
            // result -- never from inside the call that asked.
            op->Fail("not connected");
            AddToThinkQueue(op);
            return;
        }

        AddToThreadQueue(op);
    }

    void MySQLConnection::Query(const char *query, Source2ModMySQLQueryCallbackFunc callback, ...)
    {
        va_list args;
        va_start(args, callback);

        va_list vaCopy;
        va_copy(vaCopy, args);
        const int iLen = std::vsnprintf(NULL, 0, query, vaCopy);
        va_end(vaCopy);

        std::vector<char> zc(iLen + 1);
        std::vsnprintf(zc.data(), zc.size(), query, args);
        va_end(args);

        TMySQLQueryOp *op = new TMySQLQueryOp(this, std::string(zc.data(), zc.size()), callback);

        if (!m_pDatabase)
        {
            op->Fail("not connected");
            AddToThinkQueue(op);
            return;
        }

        AddToThreadQueue(op);
    }

    void MySQLConnection::ExecuteTransaction(Source2ModMySQLTransaction txn, Source2ModMySQLTransactionSuccessCallbackFunc success, Source2ModMySQLTransactionFailureCallbackFunc failure)
    {
        TMySQLTransactOp *op = new TMySQLTransactOp(this, txn, success, failure);

        // Used to go to the worker regardless and hand libmysql a null handle.
        if (!m_pDatabase)
        {
            op->Fail("not connected");
            AddToThinkQueue(op);
            return;
        }

        AddToThreadQueue(op);
    }

    void MySQLConnection::Destroy()
    {
        std::erase(mysqlManager.m_vecMysqlConnections, this);
        delete this;
    }

    void MySQLConnection::RunFrame()
    {
        if (!m_ThinkQueue.size())
        {
            return;
        }

        ThreadOperation *op;
        {
            std::lock_guard<std::mutex> lock(m_ThinkLock);
            op = m_ThinkQueue.front();
            m_ThinkQueue.pop();
        }

        op->RunThinkPart();
        delete op;
    }

    void MySQLConnection::ThreadRun()
    {
        if (mysql_thread_safe())
        {
            mysql_thread_init();
        }

        std::unique_lock<std::mutex> lock(m_Lock);

        while (true)
        {
            if (m_threadQueue.empty())
            {
                if (m_Terminate)
                {
                    return;
                }

                m_QueueEvent.wait(lock);
                continue;
            }

            ThreadOperation *op = m_threadQueue.front();
            m_threadQueue.pop();

            lock.unlock();
            op->RunThreadPart();

            {
                std::lock_guard<std::mutex> think_lock(m_ThinkLock);
                m_ThinkQueue.push(op);
            }

            lock.lock();
        }

        mysql_thread_end();
    }

    void MySQLConnection::AddToThreadQueue(ThreadOperation *threadOperation)
    {
        if (!m_thread)
        {
            m_thread = std::unique_ptr<std::thread>(new std::thread(&MySQLConnection::ThreadRun, this));
        }

        {
            std::lock_guard<std::mutex> lock(m_Lock);
            m_threadQueue.push(threadOperation);
            m_QueueEvent.notify_one();
        }
    }

    void MySQLConnection::AddToThinkQueue(ThreadOperation *threadOperation)
    {
        std::lock_guard<std::mutex> lock(m_ThinkLock);
        m_ThinkQueue.push(threadOperation);
    }

    std::string MySQLConnection::Escape(const char *string)
    {
        return Escape(const_cast<char *>(string));
    }

    std::string MySQLConnection::Escape(char *string)
    {
        size_t size = strlen(string);
        char *buffer = new char[size * 2 + 1];

        mysql_real_escape_string(m_pDatabase, buffer, string, size);

        std::string out(buffer);
        delete[] buffer;
        return out;
    }

    void ServerlessHandle::Query(const char *query, Source2ModMySQLServerlessCallbackFunc callback)
    {
        Query(query, std::vector<std::string>(), std::move(callback));
    }

    void ServerlessHandle::Query(const char *query, std::vector<std::string> params, Source2ModMySQLServerlessCallbackFunc callback)
    {
        auto *op = new ServerlessOp();

        op->m_Owner = m_Owner;
        op->m_Handle = m_Id;
        op->m_Info = m_Info;
        op->m_sQuery = query ? query : "";
        op->m_vecParams = std::move(params);
        op->m_Callback = std::move(callback);

        mysqlManager.QueueServerless(op);
    }

    void ServerlessHandle::Destroy()
    {
        mysqlManager.DestroyServerless(this);
    }

    ISource2ModMySQLConnection* MySQLManager::CreateConnection(PluginId owner, Source2ModMySQLConnectionInfo info)
    {
        auto connection = new MySQLConnection(owner, info);
        m_vecMysqlConnections.push_back(connection);

        return connection;
    }

    ISource2ModMySQLServerless* MySQLManager::CreateServerless(PluginId owner, Source2ModMySQLConnectionInfo info)
    {
        auto *handle = new ServerlessHandle(owner, m_NextServerlessId++, info);
        m_vecServerless.push_back(handle);

        return handle;
    }

    void MySQLManager::QueueServerless(ServerlessOp *op)
    {
        std::lock_guard<std::mutex> lock(m_ServerlessLock);

        // Past Shutdown() there is no worker left to run this, and starting
        // one again would outlive the library it would be running.
        if (m_bServerlessTerminate)
        {
            delete op;
            return;
        }

        if (!m_ServerlessThread)
        {
            m_ServerlessThread = std::make_unique<std::thread>(&MySQLManager::ServerlessThreadRun, this);
        }

        m_ServerlessQueue.push(op);
        m_ServerlessEvent.notify_one();
    }

    void MySQLManager::DestroyServerless(ServerlessHandle *handle)
    {
        const uint32_t id = handle->Id();
        CancelServerlessOps([id](const ServerlessOp *op) { return op->m_Handle == id; });

        std::erase(m_vecServerless, handle);
        delete handle;
    }

    void MySQLManager::ServerlessThreadRun()
    {
        if (mysql_thread_safe())
        {
            mysql_thread_init();
        }

        std::unique_lock<std::mutex> lock(m_ServerlessLock);

        while (true)
        {
            // Whatever is still queued when Source2Mod is going down is not
            // worth a round trip: nobody is left to hand it back to.
            if (m_bServerlessTerminate)
            {
                break;
            }

            if (m_ServerlessQueue.empty())
            {
                m_ServerlessEvent.wait(lock);
                continue;
            }

            ServerlessOp *op = m_ServerlessQueue.front();
            m_ServerlessQueue.pop();

            // Published so the main thread can find this one and clear its
            // callback if the plugin waiting on it goes away meanwhile.
            m_pServerlessRunning = op;

            lock.unlock();
            RunServerlessOp(op);
            lock.lock();

            m_pServerlessRunning = nullptr;

            {
                std::lock_guard<std::mutex> done(m_ServerlessDoneLock);
                m_ServerlessDone.push(op);
            }
        }

        mysql_thread_end();
    }

    void MySQLManager::RunServerlessOp(ServerlessOp *op)
    {
        MYSQL *mysql = OpenConnection(op->m_Info, &op->m_sError, &op->m_nErrorCode);
        if (!mysql)
        {
            return;
        }

        std::string query;
        if (!FormatQuery(mysql, op->m_sQuery, op->m_vecParams, &query, &op->m_sError))
        {
            mysql_close(mysql);
            return;
        }

        if (mysql_query(mysql, query.c_str()))
        {
            op->m_sError = mysql_error(mysql);
            op->m_nErrorCode = mysql_errno(mysql);

            mysql_close(mysql);
            return;
        }

        // The rows have to come off the wire and into the op here: by the time
        // the callback runs, on the main thread, this connection is closed.
        if (!ReadResults(mysql, &op->m_vecSets, &op->m_InsertId, &op->m_AffectedRows, &op->m_sError,
                         &op->m_nErrorCode))
        {
            mysql_close(mysql);
            return;
        }

        op->m_bSuccess = true;
        mysql_close(mysql);
    }

    void MySQLManager::RunServerlessFrame()
    {
        while (true)
        {
            ServerlessOp *op;
            {
                std::lock_guard<std::mutex> lock(m_ServerlessDoneLock);
                if (m_ServerlessDone.empty())
                {
                    return;
                }

                op = m_ServerlessDone.front();
                m_ServerlessDone.pop();
            }

            if (!op->m_bSuccess)
            {
                FP_WARN("Serverless MySQL query failed ({}): {}", op->m_Info.m_sDatabase, op->m_sError);
            }

            // Gone if the plugin that asked unloaded while this was in the
            // air. The op still had to be seen through, but there is nobody
            // left to hand it to.
            if (op->m_Callback)
            {
                Source2ModMySQLServerlessResult result;
                result.m_bSuccess = op->m_bSuccess;
                result.m_nErrorCode = op->m_nErrorCode;
                result.m_sError = op->m_sError;

                CMySQLStoredQuery query(std::move(op->m_vecSets), op->m_InsertId, op->m_AffectedRows);
                if (op->m_bSuccess)
                {
                    result.m_pQuery = &query;
                }

                op->m_Callback(result);
            }

            delete op;
        }
    }

    void MySQLManager::CancelServerlessOps(const std::function<bool(const ServerlessOp *)> &pred)
    {
        std::vector<ServerlessOp*> dropping;

        {
            std::lock_guard<std::mutex> lock(m_ServerlessLock);

            std::queue<ServerlessOp*> keeping;
            while (!m_ServerlessQueue.empty())
            {
                ServerlessOp *op = m_ServerlessQueue.front();
                m_ServerlessQueue.pop();

                if (pred(op))
                    dropping.push_back(op);
                else
                    keeping.push(op);
            }
            m_ServerlessQueue = std::move(keeping);

            // The worker cannot be interrupted mid-query, and waiting for it
            // would stall the server for as long as the connect timeout. Only
            // the callback has to go: it is the one part of the op that lives
            // inside the plugin being unloaded, and nothing on the worker
            // touches it.
            if (m_pServerlessRunning && pred(m_pServerlessRunning))
            {
                m_pServerlessRunning->m_Callback = nullptr;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_ServerlessDoneLock);

            std::queue<ServerlessOp*> keeping;
            while (!m_ServerlessDone.empty())
            {
                ServerlessOp *op = m_ServerlessDone.front();
                m_ServerlessDone.pop();

                if (pred(op))
                    dropping.push_back(op);
                else
                    keeping.push(op);
            }
            m_ServerlessDone = std::move(keeping);
        }

        // Here, on the main thread, while the library holding these callbacks
        // is still mapped -- destroying a std::function is running code too.
        for (auto *op : dropping)
            delete op;
    }

    void MySQLManager::RunFrame()
    {
        // A callback is free to Destroy() the connection it came from, which
        // takes that entry back out of this vector. So the round is walked
        // over a copy, and each entry checked against the live one first.
        const std::vector<MySQLConnection*> connections = m_vecMysqlConnections;

        for (auto *connection : connections)
        {
            if (std::find(m_vecMysqlConnections.begin(), m_vecMysqlConnections.end(), connection) == m_vecMysqlConnections.end())
                continue;

            connection->RunFrame();
        }

        RunServerlessFrame();
    }

    void MySQLManager::RemoveAllForPlugin(PluginId id)
    {
        CancelServerlessOps([id](const ServerlessOp *op) { return op->m_Owner == id; });

        // Destroy() erases from the same vectors, so the ones to close are
        // picked out first rather than erased while being walked.
        std::vector<ServerlessHandle*> handles;

        for (auto *handle : m_vecServerless)
        {
            if (handle->Owner() == id)
                handles.push_back(handle);
        }

        for (auto *handle : handles)
            handle->Destroy();

        std::vector<MySQLConnection*> going;

        for (auto* connection : m_vecMysqlConnections)
        {
            if (connection->Owner() == id)
                going.push_back(connection);
        }

        for (auto* connection : going)
            connection->Destroy();
    }

    void MySQLManager::Shutdown()
    {
        auto connections = std::move(m_vecMysqlConnections);
        m_vecMysqlConnections.clear();

        // Joins each worker thread and cancels what it had not got to. Not
        // Destroy(), which would look for these in the vector just emptied.
        for (auto* connection : connections)
            delete connection;

        std::unique_ptr<std::thread> thread;
        {
            std::lock_guard<std::mutex> lock(m_ServerlessLock);

            m_bServerlessTerminate = true;
            m_ServerlessEvent.notify_all();

            thread = std::move(m_ServerlessThread);
        }

        // Returns once the query it is on, if any, has been seen through.
        if (thread)
            thread->join();

        while (!m_ServerlessQueue.empty())
        {
            delete m_ServerlessQueue.front();
            m_ServerlessQueue.pop();
        }

        while (!m_ServerlessDone.empty())
        {
            delete m_ServerlessDone.front();
            m_ServerlessDone.pop();
        }

        for (auto *handle : m_vecServerless)
            delete handle;

        m_vecServerless.clear();
    }
}
