/**
 * Source2Mod SourcePawn API: HTTP (ISource2ModHTTP). Requests run off the
 * game thread; the callback comes back on a game frame. Pending callbacks
 * of an unloaded plugin are dropped.
 */

#if defined _source2mod_http_included
 #endinput
#endif
#define _source2mod_http_included

#include <source2mod/json>

enum Source2ModHTTPMethod
{
	HTTPMethod_Get = 0,
	HTTPMethod_Post,
	HTTPMethod_Put,
	HTTPMethod_Patch,
	HTTPMethod_Delete,
	HTTPMethod_Head
};

/** Request headers (std::vector<Source2ModHTTPHeader>). The request copies them. */
methodmap Source2ModHTTPHeaders < Handle
{
	public native Source2ModHTTPHeaders();
	public native void Add(const char[] name, const char[] value);
	public native int Count();
	public native void Clear();
}

/** A response; valid inside the callback only. */
methodmap Source2ModHTTPResponse < Handle
{
	/** The transfer worked (any status code). */
	public native bool Success();
	public native int StatusCode();

	/** @return Body bytes written */
	public native int GetBody(char[] buffer, int maxlen);
	public native int BodyLength();

	/** The body parsed as JSON; null when it is not JSON. Valid as long as the response. */
	public native ISource2ModJSONValue Json();
}

typedef Source2ModHTTPCallback = function void (Source2ModHTTPResponse response, any data);

methodmap Source2ModHTTP
{
	public static native bool IsAvailable();
	public static native int PendingCount();

	public static native void Request(Source2ModHTTPMethod method, const char[] url, const char[] body, Source2ModHTTPCallback callback,
	                                  any data = 0, Source2ModHTTPHeaders headers = null);

	public static native void Get(const char[] url, Source2ModHTTPCallback callback, any data = 0, Source2ModHTTPHeaders headers = null);
	public static native void Post(const char[] url, const char[] body, Source2ModHTTPCallback callback, any data = 0, Source2ModHTTPHeaders headers = null);
	public static native void Put(const char[] url, const char[] body, Source2ModHTTPCallback callback, any data = 0, Source2ModHTTPHeaders headers = null);
	public static native void Patch(const char[] url, const char[] body, Source2ModHTTPCallback callback, any data = 0, Source2ModHTTPHeaders headers = null);
	public static native void Delete(const char[] url, const char[] body, Source2ModHTTPCallback callback, any data = 0, Source2ModHTTPHeaders headers = null);
}
