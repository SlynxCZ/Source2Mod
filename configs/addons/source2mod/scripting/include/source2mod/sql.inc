/**
 * Source2Mod SourcePawn API: SQL, MySQL and SQLite (ISource2ModSQL). Queries run off the
 * game thread; callbacks come back on a game frame. Query and result
 * handles a callback is given are valid inside it only. Callbacks of a
 * deleted connection, or of an unloaded plugin, are dropped.
 *
 * 64-bit integers are two cells (value[0] low, value[1] high bits).
 */

#if defined _source2mod_sql_included
 #endinput
#endif
#define _source2mod_sql_included

/** Which database a connection talks to (ESource2ModSQLDriver). */
enum Source2ModSQLDriver
{
	SQLDriver_MySQL = 0,    /**< host, user, pass, database, port */
	SQLDriver_SQLite = 1    /**< database is the file (a relative path is under addons/source2mod/data) */
};

/** Column types: MySQL's numbering; SQLite reports LongLong, Double, String, Blob, Null. */
enum Source2ModSQLType
{
	SQLType_Decimal = 0,
	SQLType_Tiny,
	SQLType_Short,
	SQLType_Long,
	SQLType_Float,
	SQLType_Double,
	SQLType_Null,
	SQLType_Timestamp,
	SQLType_LongLong,
	SQLType_Int24,
	SQLType_Date,
	SQLType_Time,
	SQLType_DateTime,
	SQLType_Year,
	SQLType_NewDate,
	SQLType_VarChar,
	SQLType_Bit,
	SQLType_Timestamp2,
	SQLType_DateTime2,
	SQLType_Time2,
	SQLType_Unknown,
	SQLType_Json = 245,
	SQLType_NewDecimal = 246,
	SQLType_Enum = 247,
	SQLType_Set = 248,
	SQLType_TinyBlob = 249,
	SQLType_MediumBlob = 250,
	SQLType_LongBlob = 251,
	SQLType_Blob = 252,
	SQLType_VarString = 253,
	SQLType_String = 254,
	SQLType_Geometry = 255
};

/** A result set: a cursor over rows, columns by number. */
methodmap ISource2ModSQLResult < Handle
{
	public native int GetRowCount();
	public native int GetFieldCount();
	public native bool FieldNameToNum(const char[] name, int &column);

	/** @return Bytes written */
	public native int FieldNumToName(int column, char[] buffer, int maxlen);

	public native bool MoreRows();

	/** Moves to the next row. @return False when there is none */
	public native bool FetchRow();

	/** Whether the cursor is on a row. */
	public native bool CurrentRow();

	public native bool Rewind();
	public native Source2ModSQLType GetFieldType(int column);

	/** @return Bytes written */
	public native int GetString(int column, char[] buffer, int maxlen);
	public native int GetDataSize(int column);
	public native float GetFloat(int column);
	public native int GetInt(int column);
	public native void GetInt64(int column, int value[2]);
	public native bool IsNull(int column);
}

/** A finished query. */
methodmap ISource2ModSQLQuery < Handle
{
	/** null when the query returned no rows. */
	public native ISource2ModSQLResult GetResultSet();

	/** Multi-statement queries: moves to the next result set. */
	public native bool FetchMoreResults();

	public native int GetInsertId();
	public native int GetAffectedRows();
}

/** Queries run as one transaction (Source2ModSQLTransaction). */
methodmap Source2ModSQLTransaction < Handle
{
	public native Source2ModSQLTransaction();
	public native void AddQuery(const char[] query);
	public native int Count();
}

typedef Source2ModSQLConnectCallback = function void (bool success, any data);
typedef Source2ModSQLQueryCallback = function void (ISource2ModSQLQuery query, any data);

/** queries: one ISource2ModSQLQuery per transaction query, in order; valid inside the callback. */
typedef Source2ModSQLTransactionSuccess = function void (ArrayList queries, any data);
typedef Source2ModSQLTransactionFailure = function void (const char[] error, int failIndex, any data);

/** query is null when it failed. */
typedef Source2ModSQLServerlessCallback = function void (bool success, int errorCode, const char[] error, ISource2ModSQLQuery query, any data);

/** One connection. delete = Destroy(). */
methodmap ISource2ModSQLConnection < Handle
{
	public native void Connect(Source2ModSQLConnectCallback callback, any data = 0);

	/** query is null when it failed. */
	public native void Query(const char[] query, Source2ModSQLQueryCallback callback, any data = 0);

	/** Runs a copy of txn: it may be deleted right after. */
	public native void ExecuteTransaction(Source2ModSQLTransaction txn, Source2ModSQLTransactionSuccess success,
	                                      Source2ModSQLTransactionFailure failure, any data = 0);

	/** Which database this connection talks to. */
	public native Source2ModSQLDriver GetDriver();

	/** Escapes text for a query (needs a connection). @return Bytes written */
	public native int Escape(const char[] text, char[] buffer, int maxlen);
}

/** Connects per query, no Connect() needed. delete = Destroy(). */
methodmap ISource2ModSQLServerless < Handle
{
	public native void Query(const char[] query, Source2ModSQLServerlessCallback callback, any data = 0);

	/** "?" placeholders filled with params, escaped. */
	public native void QueryWithParams(const char[] query, const char[][] params, int numParams, Source2ModSQLServerlessCallback callback, any data = 0);
}

methodmap Source2ModSQL
{
	/** host, user and pass are unused for SQLite. */
	public static native ISource2ModSQLConnection CreateConnection(const char[] host, const char[] user, const char[] pass, const char[] database,
	                                                                int port = 3306, int timeout = 60, Source2ModSQLDriver driver = SQLDriver_MySQL);
	public static native ISource2ModSQLServerless CreateServerless(const char[] host, const char[] user, const char[] pass, const char[] database,
	                                                                int port = 3306, int timeout = 60, Source2ModSQLDriver driver = SQLDriver_MySQL);

	/**
	 * A connection named in configs/databases.json ("" = "default"); null
	 * when there is no such entry.
	 */
	public static native ISource2ModSQLConnection CreateNamedConnection(const char[] name);
	public static native ISource2ModSQLServerless CreateNamedServerless(const char[] name);
}
