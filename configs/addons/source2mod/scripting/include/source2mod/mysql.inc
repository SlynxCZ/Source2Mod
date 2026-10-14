/**
 * Source2Mod SourcePawn API: MySQL (ISource2ModMySQL). Queries run off the
 * game thread; callbacks come back on a game frame. Query and result
 * handles a callback is given are valid inside it only. Callbacks of a
 * deleted connection, or of an unloaded plugin, are dropped.
 *
 * 64-bit integers are two cells (value[0] low, value[1] high bits).
 */

#if defined _source2mod_mysql_included
 #endinput
#endif
#define _source2mod_mysql_included

enum Source2ModMySQLType
{
	MySQLType_Decimal = 0,
	MySQLType_Tiny,
	MySQLType_Short,
	MySQLType_Long,
	MySQLType_Float,
	MySQLType_Double,
	MySQLType_Null,
	MySQLType_Timestamp,
	MySQLType_LongLong,
	MySQLType_Int24,
	MySQLType_Date,
	MySQLType_Time,
	MySQLType_DateTime,
	MySQLType_Year,
	MySQLType_NewDate,
	MySQLType_VarChar,
	MySQLType_Bit,
	MySQLType_Timestamp2,
	MySQLType_DateTime2,
	MySQLType_Time2,
	MySQLType_Unknown,
	MySQLType_Json = 245,
	MySQLType_NewDecimal = 246,
	MySQLType_Enum = 247,
	MySQLType_Set = 248,
	MySQLType_TinyBlob = 249,
	MySQLType_MediumBlob = 250,
	MySQLType_LongBlob = 251,
	MySQLType_Blob = 252,
	MySQLType_VarString = 253,
	MySQLType_String = 254,
	MySQLType_Geometry = 255
};

/** A result set: a cursor over rows, columns by number. */
methodmap ISource2ModMySQLResult < Handle
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
	public native Source2ModMySQLType GetFieldType(int column);

	/** @return Bytes written */
	public native int GetString(int column, char[] buffer, int maxlen);
	public native int GetDataSize(int column);
	public native float GetFloat(int column);
	public native int GetInt(int column);
	public native void GetInt64(int column, int value[2]);
	public native bool IsNull(int column);
}

/** A finished query. */
methodmap ISource2ModMySQLQuery < Handle
{
	/** null when the query returned no rows. */
	public native ISource2ModMySQLResult GetResultSet();

	/** Multi-statement queries: moves to the next result set. */
	public native bool FetchMoreResults();

	public native int GetInsertId();
	public native int GetAffectedRows();
}

/** Queries run as one transaction (Source2ModMySQLTransaction). */
methodmap Source2ModMySQLTransaction < Handle
{
	public native Source2ModMySQLTransaction();
	public native void AddQuery(const char[] query);
	public native int Count();
}

typedef Source2ModMySQLConnectCallback = function void (bool success, any data);
typedef Source2ModMySQLQueryCallback = function void (ISource2ModMySQLQuery query, any data);

/** queries: one ISource2ModMySQLQuery per transaction query, in order; valid inside the callback. */
typedef Source2ModMySQLTransactionSuccess = function void (ArrayList queries, any data);
typedef Source2ModMySQLTransactionFailure = function void (const char[] error, int failIndex, any data);

/** query is null when it failed. */
typedef Source2ModMySQLServerlessCallback = function void (bool success, int errorCode, const char[] error, ISource2ModMySQLQuery query, any data);

/** One connection. delete = Destroy(). */
methodmap ISource2ModMySQLConnection < Handle
{
	public native void Connect(Source2ModMySQLConnectCallback callback, any data = 0);

	/** query is null when it failed. */
	public native void Query(const char[] query, Source2ModMySQLQueryCallback callback, any data = 0);

	/** Runs a copy of txn: it may be deleted right after. */
	public native void ExecuteTransaction(Source2ModMySQLTransaction txn, Source2ModMySQLTransactionSuccess success,
	                                      Source2ModMySQLTransactionFailure failure, any data = 0);

	/** Escapes text for a query (needs a connection). @return Bytes written */
	public native int Escape(const char[] text, char[] buffer, int maxlen);
}

/** Connects per query, no Connect() needed. delete = Destroy(). */
methodmap ISource2ModMySQLServerless < Handle
{
	public native void Query(const char[] query, Source2ModMySQLServerlessCallback callback, any data = 0);

	/** "?" placeholders filled with params, escaped. */
	public native void QueryWithParams(const char[] query, const char[][] params, int numParams, Source2ModMySQLServerlessCallback callback, any data = 0);
}

methodmap Source2ModMySQL
{
	public static native ISource2ModMySQLConnection CreateConnection(const char[] host, const char[] user, const char[] pass, const char[] database,
	                                                                  int port = 3306, int timeout = 60);
	public static native ISource2ModMySQLServerless CreateServerless(const char[] host, const char[] user, const char[] pass, const char[] database,
	                                                                  int port = 3306, int timeout = 60);
}
