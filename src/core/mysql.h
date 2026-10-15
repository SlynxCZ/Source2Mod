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
#include "source2mod/ISource2ModSQL.h"

#ifdef WIN32
#include <WinSock2.h>
#include <mysql.h>
#else
#include <mysql.h>
#endif

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace mysql {
    class CMySQLStoredQuery;
    class MySQLConnection;
    class ServerlessHandle;
    struct ServerlessOp;

    /// A set of connection details Source2Mod owns outright.
    ///
    /// Source2ModSQLConnectionInfo holds bare const char*, and a plugin has
    /// every reason to build one out of temporaries -- a std::string member,
    /// a config lookup, a literal in the same call. The worker thread reads
    /// these long after that call has returned, so the strings have to be
    /// ours rather than whatever the pointers happened to be aimed at.
    struct OwnedConnectionInfo
    {
        OwnedConnectionInfo() = default;

        explicit OwnedConnectionInfo(const Source2ModSQLConnectionInfo &info)
            : m_sHost(info.host ? info.host : ""),
              m_sUser(info.user ? info.user : ""),
              m_sPass(info.pass ? info.pass : ""),
              m_sDatabase(info.database ? info.database : ""),
              m_nPort(info.port),
              m_nTimeout(info.timeout)
        {
        }

        std::string m_sHost;
        std::string m_sUser;
        std::string m_sPass;
        std::string m_sDatabase;

        int m_nPort = 3306;
        int m_nTimeout = 60;
    };

    /// One result set, copied out of libmysql row by row.
    ///
    /// A serverless query has no connection left by the time its callback
    /// runs, and a connection query's may already be busy with the next one
    /// on the worker; a MYSQL_RES does not outlive the handle it came off. So the
    /// worker reads the whole thing into this while the connection is still
    /// up, and the main thread reads rows out of here.
    struct MySQLResultSet
    {
        struct Field
        {
            std::string m_sName;
            int m_nType = SOURCE2MOD_SQL_TYPE_UNKNOWN;
        };

        /// Binary safe: the bytes are kept with their length, not as a C string.
        struct Cell
        {
            bool m_bNull = true;
            std::string m_sData;
        };

        std::vector<Field> m_vecFields;
        std::vector<std::vector<Cell>> m_vecRows;
    };

    class ThreadOperation
    {
    public:
        virtual ~ThreadOperation() = default;

        virtual void RunThreadPart() = 0;
        virtual void CancelThinkPart() = 0;
        virtual void RunThinkPart() = 0;
    };

    class TMySQLConnectOp : public ThreadOperation
    {
    public:
        TMySQLConnectOp(MySQLConnection *con, Source2ModSQLConnectCallbackFunc func) : m_pCon(con), m_callback(func) {}

        void RunThreadPart();
        void CancelThinkPart();
        void RunThinkPart();

    private:
        MySQLConnection *m_pCon;
        Source2ModSQLConnectCallbackFunc m_callback;
        MYSQL *m_pDatabase = nullptr;
        char m_szError[255] {};
    };

    class TMySQLQueryOp : public ThreadOperation
    {
    public:
        TMySQLQueryOp(MySQLConnection *con, std::string query, Source2ModSQLQueryCallbackFunc func) : m_pCon(con), m_szQuery(query), m_callback(func) {}

        ~TMySQLQueryOp();

        void RunThreadPart();
        void CancelThinkPart();
        void RunThinkPart();

        /// Fails the op before it ever reaches the worker, so it can go
        /// straight to the think queue and still hand its callback a null.
        void Fail(const char *error);

    private:
        MySQLConnection *m_pCon;
        std::string m_szQuery;
        Source2ModSQLQueryCallbackFunc m_callback;
        CMySQLStoredQuery *m_pQuery = nullptr;
        char m_szError[255] {};
    };

    class TMySQLTransactOp : public ThreadOperation
    {
    public:
        TMySQLTransactOp(MySQLConnection *con, Source2ModSQLTransaction txn, Source2ModSQLTransactionSuccessCallbackFunc success, Source2ModSQLTransactionFailureCallbackFunc failure)
            : m_pCon(con), m_txn(txn), m_successCallback(success), m_failureCallback(failure)
        {
        }

        ~TMySQLTransactOp();

        void RunThreadPart();
        void CancelThinkPart();
        void RunThinkPart();

        /// See TMySQLQueryOp::Fail().
        void Fail(const char *error);

    private:
        MySQLConnection *m_pCon;
        Source2ModSQLTransaction m_txn;
        Source2ModSQLTransactionSuccessCallbackFunc m_successCallback;
        Source2ModSQLTransactionFailureCallbackFunc m_failureCallback;
        std::vector<ISource2ModSQLQuery *> m_pQueries;
        char m_szError[255] {};
        int m_failIndex = -1;

        bool DoSimpleQuery(const char *query);
        CMySQLStoredQuery *DoQuery(const char *query);
        void Rollback();
    };

    /// Reads rows out of a MySQLResultSet the worker filled in.
    class CMySQLStoredResult final : public ISource2ModSQLResult, ISource2ModSQLRow
    {
    public:
        explicit CMySQLStoredResult(const MySQLResultSet *set) : m_pSet(set) {}

        void Reset(const MySQLResultSet *set);

        int GetRowCount() override;
        int GetFieldCount() override;
        bool FieldNameToNum(const char *name, unsigned int *columnId) override;
        const char *FieldNumToName(unsigned int colId) override;
        bool MoreRows() override;
        ISource2ModSQLRow *FetchRow() override;
        ISource2ModSQLRow *CurrentRow() override;
        bool Rewind() override;
        int GetFieldType(unsigned int field) override;
        const char *GetString(unsigned int columnId, size_t *length = nullptr) override;
        size_t GetDataSize(unsigned int columnId) override;
        float GetFloat(unsigned int columnId) override;
        int GetInt(unsigned int columnId) override;
        bool IsNull(unsigned int columnId) override;
        int64_t GetInt64(unsigned int columnId) override;

    private:
        /// The cell the cursor is on, or null when it is off either end.
        const MySQLResultSet::Cell *Cell(unsigned int columnId) const;

        const MySQLResultSet *m_pSet;
        unsigned int m_CurRow = 0;
    };

    /// What every query hands to its callback, serverless or on a connection.
    /// Owns its rows outright, so reading it never touches libmysql: the
    /// connection behind it may be closed, or running the next query.
    class CMySQLStoredQuery final : public ISource2ModSQLQuery
    {
    public:
        CMySQLStoredQuery(std::vector<MySQLResultSet> sets, unsigned int insertId, unsigned int affectedRows);

        ISource2ModSQLResult *GetResultSet() override;
        bool FetchMoreResults() override;
        unsigned int GetInsertId() override;
        unsigned int GetAffectedRows() override;

    private:
        std::vector<MySQLResultSet> m_vecSets;
        size_t m_CurSet = 0;
        CMySQLStoredResult m_res;
        unsigned int m_insertId;
        unsigned int m_affectedRows;
    };

    class MySQLConnection : public ISource2ModSQLConnection
    {
    public:
        MySQLConnection(PluginId owner, const Source2ModSQLConnectionInfo info);
        ~MySQLConnection() override;

        PluginId Owner() const { return m_Owner; }

        void Connect(Source2ModSQLConnectCallbackFunc callback) override;
        void Query(char *query, Source2ModSQLQueryCallbackFunc callback) override;
        void Query(const char *query, Source2ModSQLQueryCallbackFunc callback, ...) override;
        void ExecuteTransaction(Source2ModSQLTransaction txn, Source2ModSQLTransactionSuccessCallbackFunc success, Source2ModSQLTransactionFailureCallbackFunc failure) override;

        void Destroy() override;
        void RunFrame();

        void SetDatabase(MYSQL *db)
        {
            m_pDatabase = db;
        }

        MYSQL *GetDatabase()
        {
            return m_pDatabase;
        }

        ESource2ModSQLDriver GetDriver() override { return SOURCE2MOD_SQL_DRIVER_MYSQL; }
        std::string Escape(char *string) override;
        std::string Escape(const char *string) override;

        OwnedConnectionInfo m_info;

    private:
        void ThreadRun();
        void AddToThreadQueue(ThreadOperation *threadOperation);
        void AddToThinkQueue(ThreadOperation *threadOperation);

        std::queue<ThreadOperation *> m_threadQueue;
        std::queue<ThreadOperation *> m_ThinkQueue;
        std::unique_ptr<std::thread> m_thread;
        std::condition_variable m_QueueEvent;
        std::mutex m_Lock;
        std::mutex m_ThinkLock;
        bool m_Terminate = false;
        MYSQL *m_pDatabase = nullptr;
        PluginId m_Owner = 0;
    };

    /// One serverless query on its way through: the details to open a
    /// connection with, the SQL, and whoever is waiting for it. Filled in on
    /// the worker, read back on the main thread, deleted there.
    struct ServerlessOp
    {
        PluginId m_Owner = 0;
        uint32_t m_Handle = 0;

        OwnedConnectionInfo m_Info;
        std::string m_sQuery;
        std::vector<std::string> m_vecParams;

        /// The only part of this op that lives inside a plugin. Cleared on the
        /// main thread when its owner goes away while the worker still has the
        /// op, which leaves the op to finish and be dropped in silence.
        Source2ModSQLServerlessCallbackFunc m_Callback;

        bool m_bSuccess = false;
        unsigned int m_nErrorCode = 0;
        std::string m_sError;
        std::vector<MySQLResultSet> m_vecSets;
        unsigned int m_InsertId = 0;
        unsigned int m_AffectedRows = 0;
    };

    /// A set of connection details a plugin holds on to. Keeps no connection
    /// of its own -- every query it is handed gets one opened and closed
    /// around it by the shared worker.
    class ServerlessHandle final : public ISource2ModSQLServerless
    {
    public:
        ServerlessHandle(PluginId owner, uint32_t id, const Source2ModSQLConnectionInfo &info)
            : m_Owner(owner), m_Id(id), m_Info(info)
        {
        }

        PluginId Owner() const { return m_Owner; }
        uint32_t Id() const { return m_Id; }

        void Query(const char *query, Source2ModSQLServerlessCallbackFunc callback) override;
        void Query(const char *query, std::vector<std::string> params, Source2ModSQLServerlessCallbackFunc callback) override;
        void Destroy() override;

    private:
        PluginId m_Owner;
        uint32_t m_Id;
        OwnedConnectionInfo m_Info;
    };

    class MySQLManager final : public ISource2ModSQL
    {
    public:
        ISource2ModSQLConnection* CreateConnection(PluginId owner, Source2ModSQLConnectionInfo info) override;
        ISource2ModSQLServerless* CreateServerless(PluginId owner, Source2ModSQLConnectionInfo info) override;
        bool GetNamedConnection(const char* name, Source2ModSQLConnectionInfo& info) override { return false; }

    public:
        /// Everything that has to happen on the main thread: each connection's
        /// think queue, then the callbacks of the serverless queries the
        /// worker has finished with. Driven by the ISource2Server vtable hook.
        void RunFrame();

        /// Destroys the connections and serverless handles this plugin never
        /// got around to destroying itself, and drops the queries it still has
        /// in the air. Has to run while its library is still mapped: the
        /// worker threads are running code from it, and every pending callback
        /// is a std::function holding more of it.
        void RemoveAllForPlugin(PluginId id);

        /// Same for whatever is left, Source2Mod's own included. The worker
        /// threads would otherwise outlive Source2Mod library itself.
        void Shutdown();

        /// Hands one query to the shared worker. Takes ownership of @p op.
        void QueueServerless(ServerlessOp *op);

        /// Drops a handle along with the queries it still has queued.
        void DestroyServerless(ServerlessHandle *handle);

        std::vector<MySQLConnection*> m_vecMysqlConnections;

    private:
        /// One thread for every serverless query there is, whatever plugin it
        /// came from: a query at a time, each on its own connection. A thread
        /// per query would be a thread per player connecting.
        void ServerlessThreadRun();

        /// Opens a connection, runs the query, reads the rows out and closes
        /// again. Everything it learns goes into the op.
        static void RunServerlessOp(ServerlessOp *op);

        /// Runs the callbacks of whatever the worker has finished with.
        void RunServerlessFrame();

        /// Forgets the ops the predicate picks out. Queued ones are deleted
        /// here and now, on the main thread, which is where their callbacks
        /// have to be destroyed; the one the worker is already on cannot be
        /// taken away from it, so its callback is cleared instead and the op
        /// is dropped without running when it comes back.
        void CancelServerlessOps(const std::function<bool(const ServerlessOp *)> &pred);

        std::vector<ServerlessHandle*> m_vecServerless;
        uint32_t m_NextServerlessId = 1;

        std::unique_ptr<std::thread> m_ServerlessThread;
        std::queue<ServerlessOp*> m_ServerlessQueue;
        ServerlessOp *m_pServerlessRunning = nullptr;
        std::queue<ServerlessOp*> m_ServerlessDone;
        std::mutex m_ServerlessLock;
        std::mutex m_ServerlessDoneLock;
        std::condition_variable m_ServerlessEvent;
        bool m_bServerlessTerminate = false;
    };

    extern MySQLManager mysqlManager;
}
