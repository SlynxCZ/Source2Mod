/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod (C)2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This file is part of the SourceMod/SourcePawn SDK.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

/**
 * Source2Mod SourcePawn API: plugin to plugin -- natives a plugin makes
 * (Source2ModNatives, the C++ ISource2ModNatives), libraries, forwards
 * (GlobalForward, PrivateForward, Source2ModCall), the calling plugin
 * (Source2ModPlugins) and the plugin lifecycle forwards. Taken from
 * SourceMod's functions.inc and sourcemod.inc; SourceMod's free natives are
 * static methods here (CreateNative -> Source2ModNatives.CreateNative,
 * Call_PushCell -> Source2ModCall.PushCell, ...).
 */

#if defined _source2mod_functions_included
 #endinput
#endif
#define _source2mod_functions_included

#include <source2mod/core>

#define SP_PARAMFLAG_BYREF  (1<<0)  /**< Internal use only. */

/**
 * Describes the various ways to pass parameters to functions or forwards.
 */
enum ParamType
{
	Param_Any           = 0,                            /**< Any data type can be pushed */
	Param_Cell          = (1<<1),                       /**< Only basic cells can be pushed */
	Param_Float         = (2<<1),                       /**< Only floats can be pushed */
	Param_String        = (3<<1)|SP_PARAMFLAG_BYREF,    /**< Only strings can be pushed */
	Param_Array         = (4<<1)|SP_PARAMFLAG_BYREF,    /**< Only arrays can be pushed */
	Param_CellByRef     = (1<<1)|SP_PARAMFLAG_BYREF,    /**< Only a cell by reference can be pushed */
	Param_FloatByRef    = (2<<1)|SP_PARAMFLAG_BYREF     /**< Only a float by reference can be pushed */
};

/**
 * Defines how a forward iterates through plugin functions.
 */
enum ExecType
{
	ET_Ignore   = 0,    /**< Ignore all return values, return 0 */
	ET_Single   = 1,    /**< Only return the last exec, ignore all others */
	ET_Event    = 2,    /**< Acts as an event with the Actions defined in core.inc, no mid-Stops allowed, returns highest */
	ET_Hook     = 3     /**< Acts as a hook with the Actions defined in core.inc, mid-Stops allowed, returns highest */
};

/**
 * @section Flags that are used with Source2ModCall.PushArrayEx() and Source2ModCall.PushStringEx()
 */

#define SM_PARAM_COPYBACK       (1<<0)      /**< Copy an array/reference back after call */

#define SM_PARAM_STRING_UTF8    (1<<0)      /**< String should be UTF-8 handled */
#define SM_PARAM_STRING_COPY    (1<<1)      /**< String should be copied into the plugin */
#define SM_PARAM_STRING_BINARY  (1<<2)      /**< Treat the string as a binary string */

/**
 * @endsection
 */

/**
 * @section Error codes
 */
#define SP_ERROR_NONE                   0   /**< No error occurred */
#define SP_ERROR_FILE_FORMAT            1   /**< File format unrecognized */
#define SP_ERROR_DECOMPRESSOR           2   /**< A decompressor was not found */
#define SP_ERROR_HEAPLOW                3   /**< Not enough space left on the heap */
#define SP_ERROR_PARAM                  4   /**< Invalid parameter or parameter type */
#define SP_ERROR_INVALID_ADDRESS        5   /**< A memory address was not valid */
#define SP_ERROR_NOT_FOUND              6   /**< The object in question was not found */
#define SP_ERROR_INDEX                  7   /**< Invalid index parameter */
#define SP_ERROR_STACKLOW               8   /**< Not enough space left on the stack */
#define SP_ERROR_NOTDEBUGGING           9   /**< Debug mode was not on or debug section not found */
#define SP_ERROR_INVALID_INSTRUCTION    10  /**< Invalid instruction was encountered */
#define SP_ERROR_MEMACCESS              11  /**< Invalid memory access */
#define SP_ERROR_STACKMIN               12  /**< Stack went beyond its minimum value */
#define SP_ERROR_HEAPMIN                13  /**< Heap went beyond its minimum value */
#define SP_ERROR_DIVIDE_BY_ZERO         14  /**< Division by zero */
#define SP_ERROR_ARRAY_BOUNDS           15  /**< Array index is out of bounds */
#define SP_ERROR_INSTRUCTION_PARAM      16  /**< Instruction had an invalid parameter */
#define SP_ERROR_STACKLEAK              17  /**< A native leaked an item on the stack */
#define SP_ERROR_HEAPLEAK               18  /**< A native leaked an item on the heap */
#define SP_ERROR_ARRAY_TOO_BIG          19  /**< A dynamic array is too big */
#define SP_ERROR_TRACKER_BOUNDS         20  /**< Tracker stack is out of bounds */
#define SP_ERROR_INVALID_NATIVE         21  /**< Native was pending or invalid */
#define SP_ERROR_PARAMS_MAX             22  /**< Maximum number of parameters reached */
#define SP_ERROR_NATIVE                 23  /**< Error originates from a native */
#define SP_ERROR_NOT_RUNNABLE           24  /**< Function or plugin is not runnable */
#define SP_ERROR_ABORTED                25  /**< Function call was aborted */

/**
 * @endsection
 */

methodmap GlobalForward < Handle {
	// Creates a global forward.
	//
	// @note The name used to create the forward is used as its public function in all target plugins.
	// @note This is ideal for global, static forwards that are never changed.
	// @note Global forwards cannot be cloned.
	// @note Use delete / Close() to destroy these.
	//
	// @param name              Name of public function to use in forward.
	// @param type              Execution type to be used.
	// @param ...               Variable number of parameter types (up to 32).
	// @return                  Handle to new global forward.
	// @error                   More than 32 parameter types passed.
	public native GlobalForward(const char[] name, ExecType type, ParamType ...);

	// Returns the number of functions in a global or private forward's call list.
	property int FunctionCount {
		public native get();
	}
};

methodmap PrivateForward < GlobalForward {
	// Creates a private forward.
	//
	// @note No functions are automatically added. Use AddFunction() to do this.
	// @note Private forwards can be cloned.
	// @note Use delete / Close() to destroy these.
	//
	// @param type          Execution type to be used.
	// @param ...           Variable number of parameter types (up to 32).
	// @return              Handle to new private forward.
	// @error               More than 32 parameter types passed.
	public native PrivateForward(ExecType type, ParamType ...);

	// Adds a function to a private forward's call list.
	//
	// @note Cannot be used during an incomplete call.
	//
	// @param plugin        Handle of the plugin that contains the function.
	//                      Pass INVALID_HANDLE to specify the calling plugin.
	// @param func          Function to add to forward.
	// @return              True on success, false otherwise.
	// @error               Invalid or corrupt private forward handle, invalid or corrupt plugin handle, or invalid function.
	public native bool AddFunction(Handle plugin, Function func);

	// Removes a function from a private forward's call list.
	//
	// @note Only removes one instance.
	// @note Functions will be removed automatically if their parent plugin is unloaded.
	//
	// @param plugin        Handle of the plugin that contains the function.
	//                      Pass INVALID_HANDLE to specify the calling plugin.
	// @param func          Function to remove from forward.
	// @return              True on success, false otherwise.
	// @error               Invalid or corrupt private forward handle, invalid or corrupt plugin handle, or invalid function.
	public native bool RemoveFunction(Handle plugin, Function func);

	// Removes all instances of a plugin from a private forward's call list.
	//
	// @note Functions will be removed automatically if their parent plugin is unloaded.
	//
	// @param plugin        Handle of the plugin to remove instances of.
	//                      Pass INVALID_HANDLE to specify the calling plugin.
	// @return              Number of functions removed from forward.
	// @error               Invalid or corrupt private forward handle or invalid or corrupt plugin handle.
	public native int RemoveAllFunctions(Handle plugin);
};

/**
 * Calls a function or a forward's call list: Start*, Push* the arguments in
 * order, then Finish (or Cancel).
 */
methodmap Source2ModCall
{
	// Gets a function id from a function name.
	//
	// @param plugin        Handle of the plugin that contains the function.
	//                      Pass INVALID_HANDLE to search in the calling plugin.
	// @param name          Name of the function.
	// @return              Function id or INVALID_FUNCTION if not found.
	// @error               Invalid or corrupt plugin handle.
	public static native Function GetFunctionByName(Handle plugin, const char[] name);

	// Starts a call to functions in a forward's call list.
	//
	// @note Cannot be used during an incomplete call.
	//
	// @param fwd           Handle to global or private forward.
	// @error               Invalid or corrupt forward handle or called before another call has completed.
	public static native void StartForward(Handle fwd);

	// Starts a call to a function.
	//
	// @note Cannot be used during an incomplete call.
	//
	// @param plugin        Handle of the plugin that contains the function.
	//                      Pass INVALID_HANDLE to specify the calling plugin.
	// @param func          Function to call.
	// @error               Invalid or corrupt plugin handle, invalid function, or called before another call has completed.
	public static native void StartFunction(Handle plugin, Function func);

	// Pushes a cell onto the current call.
	//
	// @param value         Cell value to push.
	// @error               Called before a call has been started.
	public static native void PushCell(any value);

	// Pushes a cell by reference onto the current call.
	//
	// @param value         Cell reference to push.
	// @error               Called before a call has been started.
	public static native void PushCellRef(any &value);

	// Pushes a float onto the current call.
	//
	// @param value         Floating point value to push.
	// @error               Called before a call has been started.
	public static native void PushFloat(float value);

	// Pushes a float by reference onto the current call.
	//
	// @param value         Floating point reference to push.
	// @error               Called before a call has been started.
	public static native void PushFloatRef(float &value);

	// Pushes an array onto the current call.
	//
	// @note Changes to array are not copied back to caller. Use PushArrayEx() to do this.
	//
	// @param value         Array to push.
	// @param size          Size of array.
	// @error               Called before a call has been started.
	public static native void PushArray(const any[] value, int size);

	// Pushes an array onto the current call.
	//
	// @param value         Array to push.
	// @param size          Size of array.
	// @param cpflags       Whether or not changes should be copied back to the input array.
	//                      See SM_PARAM_* constants for details.
	// @error               Called before a call has been started.
	public static native void PushArrayEx(any[] value, int size, int cpflags);

	// Pushes the NULL_VECTOR onto the current call.
	// @see IsNullVector
	//
	// @error               Called before a call has been started.
	public static native void PushNullVector();

	// Pushes a string onto the current call.
	//
	// @note Changes to string are not copied back to caller. Use PushStringEx() to do this.
	//
	// @param value         String to push.
	// @error               Called before a call has been started.
	public static native void PushString(const char[] value);

	// Pushes a string onto the current call.
	//
	// @param value         String to push.
	// @param length        Length of string buffer.
	// @param szflags       Flags determining how string should be handled.
	//                      See SM_PARAM_STRING_* constants for details.
	//                      The default (0) is to push ASCII.
	// @param cpflags       Whether or not changes should be copied back to the input array.
	//                      See SM_PARAM_* constants for details.
	// @error               Called before a call has been started.
	public static native void PushStringEx(char[] value, int length, int szflags, int cpflags);

	// Pushes the NULL_STRING onto the current call.
	// @see IsNullString
	//
	// @error               Called before a call has been started.
	public static native void PushNullString();

	// Completes a call to a function or forward's call list.
	//
	// @param result        Return value of function or forward's call list.
	// @return              SP_ERROR_NONE on success, any other integer on failure.
	// @error               Called before a call has been started.
	public static native int Finish(any &result=0);

	// Cancels a call to a function or forward's call list.
	//
	// @error               Called before a call has been started.
	public static native void Cancel();
}

typeset NativeCall
{
	/**
	 * Defines a native function.
	 *
	 * It is not necessary to validate the parameter count
	 *
	 * @param plugin        Handle of the calling plugin.
	 * @param numParams     Number of parameters passed to the native.
	 * @return              Value for the native call to return.
	 */
	function int (Handle plugin, int numParams);

	/**
	 * Defines a native function.
	 *
	 * It is not necessary to validate the parameter count
	 *
	 * @param plugin         Handle of the calling plugin.
	 * @param numParams      Number of parameters passed to the native.
	 * @return               Value for the native call to return.
	 */
	function any (Handle plugin, int numParams);

	/**
	 * Defines a native function.
	 *
	 * It is not necessary to validate the parameter count
	 *
	 * @param plugin         Handle of the calling plugin.
	 * @param numParams      Number of parameters passed to the native.
	 */
	function void (Handle plugin, int numParams);
}

/**
 * Natives a plugin makes for other plugins, and libraries (C++
 * ISource2ModNatives). The Get / Set / Is / FormatString methods work on the
 * parameters of the native call being served, numbered from 1.
 */
methodmap Source2ModNatives
{
	// Creates a dynamic native.  This should only be called in AskPluginLoad2(), or
	// else you risk not having your native shared with other plugins.
	//
	// @param name          Name of the dynamic native; must be unique among
	//                      all other registered dynamic natives.
	// @param func          Function to use as the dynamic native.
	public static native void CreateNative(const char[] name, NativeCall func);

	// Throws an error in the calling plugin of a native, instead of your own plugin.
	//
	// @param error         Error code to use.
	// @param fmt           Error message format.
	// @param ...           Format arguments.
	// @noreturn
	// @error               Always!
	public static native int ThrowError(int error, const char[] fmt, any ...);

	// Retrieves the string length from a native parameter string.  This is useful for
	// fetching the entire string using dynamic arrays.
	//
	// @note If this function succeeds, GetString/SetString will also succeed.
	//
	// @param param         Parameter number, starting from 1.
	// @param length        Stores the length of the string.
	// @return              SP_ERROR_NONE on success, any other integer on failure.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native int GetStringLength(int param, int &length);

	// Retrieves a string from a native parameter.
	//
	// @note Output conditions are undefined on failure.
	//
	// @param param         Parameter number, starting from 1.
	// @param buffer        Buffer to store the string in.
	// @param maxlength     Maximum length of the buffer.
	// @param bytes         Optionally store the number of bytes written.
	// @return              SP_ERROR_NONE on success, any other integer on failure.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native int GetString(int param, char[] buffer, int maxlength, int &bytes=0);

	// Sets a string in a native parameter.
	//
	// @note Output conditions are undefined on failure.
	//
	// @param param         Parameter number, starting from 1.
	// @param source        Source string to use.
	// @param maxlength     Maximum number of bytes to write.
	// @param utf8          If false, string will not be written
	//                      with UTF8 safety.
	// @param bytes         Optionally store the number of bytes written.
	// @return              SP_ERROR_NONE on success, any other integer on failure.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native int SetString(int param, const char[] source, int maxlength, bool utf8=true, int &bytes=0);

	// Gets a cell from a native parameter.
	//
	// @param param         Parameter number, starting from 1.
	// @return              Cell value at the parameter number.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native any GetCell(int param);

	// Gets a function pointer from a native parameter.
	//
	// @param param         Parameter number, starting from 1.
	// @return              Function pointer at the given parameter number.
	// @error               Invalid parameter number, or calling from a non-native function.
	public static native Function GetFunction(int param);

	// Gets a cell from a native parameter, by reference.
	//
	// @param param         Parameter number, starting from 1.
	// @return              Cell value at the parameter number.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native any GetCellRef(int param);

	// Sets a cell from a native parameter, by reference.
	//
	// @param param         Parameter number, starting from 1.
	// @param value         Cell value at the parameter number to set by reference.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native void SetCellRef(int param, any value);

	// Gets an array from a native parameter (always by reference).
	//
	// @param param         Parameter number, starting from 1.
	// @param local         Local array to copy into.
	// @param size          Maximum size of local array.
	// @return              SP_ERROR_NONE on success, anything else on failure.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native int GetArray(int param, any[] local, int size);

	// Copies a local array into a native parameter array (always by reference).
	//
	// @param param         Parameter number, starting from 1.
	// @param local         Local array to copy from.
	// @param size          Size of the local array to copy.
	// @return              SP_ERROR_NONE on success, anything else on failure.
	// @error               Invalid parameter number or calling from a non-native function.
	public static native int SetArray(int param, const any[] local, int size);

	// Check if the native parameter is the NULL_VECTOR.
	//
	// @param param         Parameter number, starting from 1.
	// @return              True if NULL_VECTOR, false otherwise.
	public static native bool IsParamNullVector(int param);

	// Check if the native parameter is the NULL_STRING.
	//
	// @param param         Parameter number, starting from 1.
	// @return              True if NULL_STRING, false otherwise.
	public static native bool IsParamNullString(int param);

	// Formats a string using parameters from a native.
	//
	// @note All parameter indexes start at 1.
	// @note If the input and output buffers overlap, the contents
	//       of the output buffer at the end is undefined.
	// @note Source2Mod takes the format from fmt_param only (fmt_string is
	//       not supported) and its arguments must follow it.
	//
	// @param out_param     Output parameter number to write to.  If 0, out_string is used.
	// @param fmt_param     Format parameter number.
	// @param vararg_param  First variable parameter number (fmt_param + 1, or 0).
	// @param out_len       Output string buffer maximum length (always required).
	// @param written       Optionally stores the number of bytes written.
	// @param out_string    Output string buffer to use if out_param is not used.
	// @param fmt_string    Unused; pass fmt_param.
	// @return              SP_ERROR_NONE on success, anything else on failure.
	public static native int FormatString(int out_param,
	                                      int fmt_param,
	                                      int vararg_param,
	                                      int out_len,
	                                      int &written=0,
	                                      char[] out_string="",
	                                      const char[] fmt_string="");

	// Registers a library name for identifying as a dependency to
	// other plugins.
	//
	// @param name          Library name.
	public static native void RegisterLibrary(const char[] name);

	// Returns whether a library exists.  This function should be considered
	// expensive; it should only be called on plugin to determine availability
	// of resources.  Use OnLibraryAdded()/OnLibraryRemoved() to detect changes
	// in libraries.
	//
	// @param name          Library name of a plugin or native plugin.
	// @return              True if exists, false otherwise.
	public static native bool LibraryExists(const char[] name);
}

/**
 * The calling plugin.
 */
methodmap Source2ModPlugins
{
	// Returns the calling plugin's Handle.
	//
	// @return              Handle of the calling plugin.
	public static native Handle GetMyHandle();

	// Returns a plugin's file name.
	//
	// @param plugin        Plugin Handle (INVALID_HANDLE uses the calling plugin).
	// @param buffer        Buffer to the store the file name.
	// @param maxlength     Maximum length of the name buffer.
	// @error               Invalid Handle.
	public static native void GetFilename(Handle plugin, char[] buffer, int maxlength);

	// Causes the plugin to enter a failed state.  An error will be thrown and
	// the plugin will be paused until it is unloaded or reloaded.
	//
	// If no extra arguments are passed, no formatting is applied.  If one or
	// more additional arguments is passed, the string is formatted.
	//
	// This function does not return, and no further code in the plugin is
	// executed.
	//
	// @param string        Format specifier string.
	// @param ...           Formatting arguments.
	// @error               Always throws SP_ERROR_ABORT.
	public static native void SetFailState(const char[] string, any ...);

	// Aborts the current callback and throws an error.  This function
	// does not return in that no code is executed following it.
	//
	// @param fmt           String format.
	// @param ...           Format arguments.
	// @noreturn
	// @error               Always!
	public static native void ThrowError(const char[] fmt, any ...);
}

/**
 * @section Plugin lifecycle forwards.
 */

enum APLRes
{
	APLRes_Success = 0,     /**< Plugin should load */
	APLRes_Failure,         /**< Plugin shouldn't load and should display an error */
	APLRes_SilentFailure    /**< Plugin shouldn't load but do so silently */
};

/**
 * Called before OnPluginStart, in case the plugin wants to check for load failure.
 * Any natives from other plugins are not available at this point.  Thus, this
 * forward should only be used for explicit pre-emptive things, such as adding
 * dynamic natives (Source2ModNatives.CreateNative) and libraries
 * (Source2ModNatives.RegisterLibrary).
 *
 * @note It is not safe to call externally resolved natives until OnPluginStart().
 * @note Any sort of RTE in this function will cause the plugin to fail loading.
 * @note If you do not return anything, it is treated like returning success.
 *
 * @param myself        Handle to the plugin.
 * @param late          Whether or not the plugin was loaded "late" (after map load).
 * @param error         Error message buffer in case load failed.
 * @param err_max       Maximum number of characters for error message buffer.
 * @return              APLRes_Success for load success, APLRes_Failure or APLRes_SilentFailure otherwise
 */
forward APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int err_max);

/**
 * Called when the plugin is fully initialized and all known external references
 * are resolved. This is only called once in the lifetime of the plugin, and is
 * paired with OnPluginEnd().
 *
 * If any run-time error is thrown during this callback, the plugin will be marked
 * as failed.
 */
forward void OnPluginStart();

/**
 * Called when the plugin is about to be unloaded.
 *
 * It is not necessary to close any handles or remove hooks in this function.
 * Source2Mod guarantees that plugin shutdown automatically and correctly releases
 * all resources.
 */
forward void OnPluginEnd();

/**
 * Called after a library is added.
 * A library is a name a plugin or native plugin registered
 * (Source2ModNatives.RegisterLibrary).
 *
 * @param name          Library name.
 */
forward void OnLibraryAdded(const char[] name);

/**
 * Called right before a library is removed.
 *
 * @param name          Library name.
 */
forward void OnLibraryRemoved(const char[] name);

/**
 * This is called once per map, any time before OnConfigsExecuted(), after
 * the "exec source2mod.cfg" command and all AutoExecConfig() exec commands
 * have been added to the server command buffer.
 *
 * If you need to load per-map settings that override default values,
 * adding commands to the server command buffer here will guarantee
 * that they're set before OnConfigsExecuted().
 *
 * Unlike OnConfigsExecuted(), this is not called on late loads.
 */
forward void OnAutoConfigsBuffered();

/**
 * Called when the map has loaded, servercfgfile (server.cfg) has been
 * executed, and all plugin configs are done executing.  This is the best
 * place to initialize plugin functions which are based on cvar data.
 *
 * @note This will always be called once and only once per map.
 */
forward void OnConfigsExecuted();

/**
 * @endsection
 */
