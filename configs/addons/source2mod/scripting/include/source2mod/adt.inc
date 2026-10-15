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
 * Source2Mod SourcePawn API: ArrayList, ArrayStack, StringMap, IntMap and their snapshots, and the sort enums (SourceMod's adt_array.inc, adt_stack.inc, adt_trie.inc, sorting.inc). Close one with delete.
 */

#if defined _source2mod_adt_included
 #endinput
#endif
#define _source2mod_adt_included

#include <source2mod/core>

/**
 * Contains sorting orders.
 */
enum SortOrder
{
	Sort_Ascending = 0,     /**< Ascending order */
	Sort_Descending = 1,    /**< Descending order */
	Sort_Random = 2         /**< Random order */
};

/**
 * Data types for ADT Array Sorts
 */
enum SortType
{
	Sort_Integer = 0,
	Sort_Float,
	Sort_String
};

/**
 * Sort comparison function for ADT Array elements. Function provides you with
 * indexes currently being sorted, use ADT Array functions to retrieve the
 * index values and compare.
 *
 * @param index1        First index to compare.
 * @param index2        Second index to compare.
 * @param array         Array that is being sorted (order is undefined).
 * @param hndl          Handle optionally passed in while sorting.
 * @return              -1 if first should go before second
 *                      0 if first is equal to second
 *                      1 if first should go after second
 */
typedef SortFuncADTArray = function int (int index1, int index2, Handle array, Handle hndl);

methodmap ArrayList < Handle {
	// Creates a dynamic global cell array.  While slower than a normal array,
	// it can be used globally AND dynamically, which is otherwise impossible.
	//
	// The contents of the array are uniform; i.e. storing a string at index X
	// and then retrieving it as an integer is NOT the same as StringToInt()!
	// The "blocksize" determines how many cells each array slot has; it cannot
	// be changed after creation.
	//
	// @param blocksize     The number of cells each member of the array can
	//                      hold.  For example, 32 cells is equivalent to:
	//                      new Array[X][32]
	// @param startsize     Initial size of the array.  Note that data will
	//                      NOT be auto-initialized.
	// @return              New Handle to the array object.
	public native ArrayList(int blocksize=1, int startsize=0);

	// Clears an array of all entries.  This is the same as Resize(0).
	public native void Clear();

	// Clones an array, returning a new handle with the same size and data.
	// This should NOT be confused with CloneHandle. This is a completely new
	// handle with the same data but no relation to the original. It should be
	// closed when no longer needed.
	//
	// @return              New handle to the cloned array object
	public native ArrayList Clone();

	// Resizes an array.  If the size is smaller than the current size, the
	// array is truncated.  If the size is larger than the current size,
	// the data at the additional indexes will not be initialized.
	//
	// @param newsize       New size.
	public native void Resize(int newsize);

	// Pushes a value onto the end of an array, adding a new index.
	//
	// This may safely be used even if the array has a blocksize greater
	// than 1.
	//
	// @param value         Value to push.
	// @return              Index of the new entry.
	// @error               Invalid Handle or out of memory.
	public native int Push(any value);

	// Pushes a string onto the end of an array, truncating it if it is too big.
	//
	// @param value         String to push.
	// @return              Index of the new entry.
	public native int PushString(const char[] value);

	// Pushes an array of cells onto the end of an array.  The cells
	// are pushed as a block (i.e. the entire array sits at the index),
	// rather than pushing each cell individually.
	//
	// @param values        Block of values to copy.
	// @param size          If not set, the number of elements copied from the array
	//                      will be equal to the blocksize.  If set higher than the
	//                      blocksize, the operation will be truncated.
	// @return              Index of the new entry.
	public native int PushArray(const any[] values, int size=-1);

	// Retrieves a cell value from an array.
	//
	// @param index         Index in the array.
	// @param block         Optionally specify which block to read from
	//                      (useful if the blocksize > 0).
	// @param asChar        Optionally read as a byte instead of a cell.
	// @return              Value read.
	// @error               Invalid index.
	public native any Get(int index, int block=0, bool asChar=false);

	// Retrieves a string value from an array.
	//
	// @param index         Index in the array.
	// @param buffer        Buffer to copy to.
	// @param maxlength     Maximum size of the buffer.
	// @param block         Optionally specify which block to read from
	//                      (useful if the blocksize > 0).
	// @return              Number of characters copied.
	// @error               Invalid index.
	public native int GetString(int index, char[] buffer, int maxlength, int block=0);

	// Retrieves an array of cells from an array.
	//
	// @param index         Index in the array.
	// @param buffer        Buffer to store the array in.
	// @param size          If not set, assumes the buffer size is equal to the
	//                      blocksize.  Otherwise, the size passed is used.
	// @param block         Optionally specify which block to read from
	//                      (useful if the blocksize > 0).
	// @return              Number of cells copied.
	// @error               Invalid index.
	public native int GetArray(int index, any[] buffer, int size=-1, int block=0);

	// Sets a cell value in an array.
	//
	// @param index         Index in the array.
	// @param value         Cell value to set.
	// @param block         Optionally specify which block to write to
	//                      (useful if the blocksize > 0).
	// @param asChar        Optionally set as a byte instead of a cell.
	// @error               Invalid index, or invalid block.
	public native void Set(int index, any value, int block=0, bool asChar=false);

	// Sets a string value in an array.
	//
	// @param index         Index in the array.
	// @param value         String value to set.
	// @param size          If not set, assumes the buffer size is equal to the
	//                      blocksize.  Otherwise, the size passed is used.
	// @param block         Optionally specify which block to write to
	//                      (useful if the blocksize > 0).
	// @return              Number of characters copied.
	// @error               Invalid index.
	public native int SetString(int index, const char[] value, int size=-1, int block=0);

	// Sets an array of cells in an array.
	//
	// @param index         Index in the array.
	// @param values        Array to copy.
	// @param size          If not set, assumes the buffer size is equal to the
	//                      blocksize.  Otherwise, the size passed is used.
	// @param block         Optionally specify which block to write to
	//                      (useful if the blocksize > 0).
	// @return              Number of cells copied.
	// @error               Invalid index.
	public native int SetArray(int index, const any[] values, int size=-1, int block=0);

	// Shifts an array up.  All array contents after and including the given
	// index are shifted up by one, and the given index is then "free."
	// After shifting, the contents of the given index is undefined.
	//
	// @param index         Index in the array to shift up from.
	// @error               Invalid index.
	public native void ShiftUp(int index);

	// Removes an array index, shifting the entire array down from that position
	// on.  For example, if item 8 of 10 is removed, the last 3 items will then be
	// (6,7,8) instead of (7,8,9), and all indexes before 8 will remain unchanged.
	//
	// @param index         Index in the array to remove at.
	// @error               Invalid index.
	public native void Erase(int index);

	// Swaps two items in the array.
	//
	// @param index1        First index.
	// @param index2        Second index.
	// @error               Invalid index.
	public native void SwapAt(int index1, int index2);

	// Returns the index for the first occurrence of the provided string. If
	// the string cannot be located, -1 will be returned.
	//
	// @param item          String to search for.
	// @param block         Optionally which block to search in.
	// @param start         Index to start searching from (exclusive), or -1 for direction default.
	//                      Valid numbers are in the interval [-1..Length].
	// @param reverse       Whether the search direction should be reversed.
	// @param caseSensitive If true (default), comparison is case sensitive.
	//                      If false, comparison is case insensitive.
	// @return              Array index, or -1 on failure
	// @error               Invalid block, or invalid start index.
	public native int FindString(const char[] item, int block=0, int start=-1, bool reverse=false, bool caseSensitive=true);

	// Returns the index for the first occurrence of the provided value. If the
	// value cannot be located, -1 will be returned.
	//
	// @param item          Value to search for.
	// @param block         Optionally which block to search in.
	// @param start         Index to start searching from (exclusive), or -1 for direction default.
	//                      Valid numbers are in the interval [-1..Length].
	// @param reverse       Whether the search direction should be reversed.
	// @return              Array index, or -1 on failure.
	// @error               Invalid block, or invalid start index.
	public native int FindValue(any item, int block=0, int start=-1, bool reverse=false);

	// Sort an ADT Array. Specify the type as Integer, Float, or String.
	//
	// @param order         Sort order to use, same as other sorts.
	// @param type          Data type stored in the ADT Array
	public native void Sort(SortOrder order, SortType type);

	// Custom sorts an ADT Array. You must pass in a comparison function.
	//
	// @param sortfunc      Sort comparison function to use
	// @param hndl          Optional Handle to pass through the comparison calls.
	public native void SortCustom(SortFuncADTArray sortfunc, Handle hndl=INVALID_HANDLE); 

	// Retrieve the size of the array.
	property int Length {
		public native get();
	}
	
	// Retrieve the blocksize the array was created with.
	property int BlockSize {
		public native get();
	}
};

methodmap ArrayStack < Handle
{
	// Creates a stack structure.  A stack is a LIFO (last in, first out) 
	// vector (array) of items.  It has O(1) insertion and O(1) removal.
	//
	// Stacks have two operations: Push (adding an item) and Pop (removes 
	// items in reverse-push order).
	// 
	// The contents of the stack are uniform; i.e. storing a string and then 
	// retrieving it as an integer is NOT the same as StringToInt()!
	//
	// The "blocksize" determines how many cells each slot has; it cannot
	// be changed after creation.
	//
	// @param blocksize    The number of cells each entry in the stack can 
	//                     hold.  For example, 32 cells is equivalent to:
	//                     new Array[X][32]
	public native ArrayStack(int blocksize=1);

	// Clears a stack of all entries.
	public native void Clear();

	// Clones an stack, returning a new handle with the same size and data.
	// This should NOT be confused with CloneHandle. This is a completely new
	// handle with the same data but no relation to the original. It should
	// closed when no longer needed.
	//
	// @return             New handle to the cloned stack object
	public native ArrayStack Clone();

	// Pushes a value onto the end of the stack, adding a new index.
	//
	// This may safely be used even if the stack has a blocksize
	// greater than 1.
	//
	// @param value        Value to push.
	public native void Push(any value);

	// Pushes a copy of a string onto the end of a stack, truncating it if it
	// is too big.
	//
	// @param value        String to push.
	public native void PushString(const char[] value);

	// Pushes a copy of an array of cells onto the end of a stack. The cells
	// are pushed as a block (i.e. the entire array takes up one stack slot),
	// rather than pushing each cell individually.
	//
	// @param stack        Stack Handle.
	// @param values       Block of values to copy.
	// @param size         If not set, the number of elements copied from the array
	//                     will be equal to the blocksize.  If set higher than the 
	//                     blocksize, the operation will be truncated.
	public native void PushArray(const any[] values, int size=-1);

	// Pops a cell value from a stack.
	//
	// @param block        Optionally specify which block to read from
	//                     (useful if the blocksize > 0).
	// @param asChar       Optionally read as a byte instead of a cell.
	// @return             Value popped from the stack.
	// @error              The stack is empty.
	public native any Pop(int block=0, bool asChar=false);

	// Reads a cell value from a stack without removing it.
	//
	// @param block        Optionally specify which block to read from
	//                     (useful if the blocksize > 0).
	// @param asChar       Optionally read as a byte instead of a cell.
	// @return             Value read from the stack.
	// @error              The stack is empty.
	public native any Top(int block=0, bool asChar=false);

	// Pops a string value from a stack.
	//
	// @param buffer       Buffer to store string.
	// @param maxlength    Maximum size of the buffer.
	// @param written      Number of characters written to buffer, not including
	//                     the null terminator.
	// @error              The stack is empty.
	public native void PopString(char[] buffer, int maxlength, int &written = 0);

	// Reads a string value from a stack without removing it.
	//
	// @param buffer       Buffer to store string.
	// @param maxlength    Maximum size of the buffer.
	// @param written      Number of characters written to buffer, not including
	//                     the null terminator.
	// @error              The stack is empty.
	public native void TopString(char[] buffer, int maxlength, int &written = 0);

	// Pops an array of cells from a stack.
	//
	// @param buffer       Buffer to store the array in.
	// @param size         If not set, assumes the buffer size is equal to the
	//                     blocksize.  Otherwise, the size passed is used.
	// @error              The stack is empty.
	public native void PopArray(any[] buffer, int size=-1);

	// Reads an array of cells from a stack without removing it.
	//
	// @param buffer       Buffer to store the array in.
	// @param size         If not set, assumes the buffer size is equal to the
	//                     blocksize.  Otherwise, the size passed is used.
	// @error              The stack is empty.
	public native void TopArray(any[] buffer, int size=-1);

	// Returns true if the stack is empty, false otherwise.
	property bool Empty {
		public native get();
	}
	
	// Retrieve the blocksize the stack was created with.
	property int BlockSize {
		public native get();
	}
	
	property int Length {
		public native get();
	}
};

/* Object-oriented wrapper for maps. */
methodmap StringMap < Handle
{
	// Creates a hash map. A hash map is a container that can map strings (called
	// "keys") to arbitrary values (cells, arrays, or strings). Keys in a hash map
	// are unique. That is, there is at most one entry in the map for a given key.
	//
	// Insertion, deletion, and lookup in a hash map are all considered to be fast
	// operations, amortized to O(1), or constant time.
	//
	// The word "Trie" in this API is historical. As of Source2Mod 1.6, tries have
	// been internally replaced with hash tables, which have O(1) insertion time
	// instead of O(n).
	//
	// The StringMap must be freed via delete or CloseHandle().
	public native StringMap();

	// Clones a string map, returning a new handle with the same size and data.
	// This should NOT be confused with CloneHandle. This is a completely new
	// handle with the same data but no relation to the original. It should be
	// closed when no longer needed with delete or CloseHandle().
	//
	// @return              New handle to the cloned string map
	public native StringMap Clone();

	// Sets a value in a hash map, either inserting a new entry or replacing an old one.
	//
	// @param key        Key string.
	// @param value      Value to store at this key.
	// @param replace    If false, operation will fail if the key is already set.
	// @return           True on success, false on failure.
	public native bool SetValue(const char[] key, any value, bool replace=true);

	// Sets an array value in a Map, either inserting a new entry or replacing an old one.
	//
	// @param key        Key string.
	// @param array      Array to store.
	// @param num_items  Number of items in the array.
	// @param replace    If false, operation will fail if the key is already set.
	// @return           True on success, false on failure.
	public native bool SetArray(const char[] key, const any[] array, int num_items, bool replace=true);

	// Sets a string value in a Map, either inserting a new entry or replacing an old one.
	//
	// @param key        Key string.
	// @param value      String to store.
	// @param replace    If false, operation will fail if the key is already set.
	// @return           True on success, false on failure.
	public native bool SetString(const char[] key, const char[] value, bool replace=true);

	// Retrieves a value in a Map.
	//
	// @param key        Key string.
	// @param value      Variable to store value.
	// @return           True on success.  False if the key is not set, or the key is set 
	//                   as an array or string (not a value).
	public native bool GetValue(const char[] key, any &value);

	// Retrieves an array in a Map.
	//
	// @param key        Key string.
	// @param array      Buffer to store array.
	// @param max_size   Maximum size of array buffer.
	// @param size       Optional parameter to store the number of elements written to the buffer.
	// @return           True on success.  False if the key is not set, or the key is set 
	//                   as a value or string (not an array).
	public native bool GetArray(const char[] key, any[] array, int max_size, int &size=0);

	// Retrieves a string in a Map.
	//
	// @param key        Key string.
	// @param value      Buffer to store value.
	// @param max_size   Maximum size of string buffer.
	// @param size       Optional parameter to store the number of bytes written to the buffer.
	// @return           True on success.  False if the key is not set, or the key is set 
	//                   as a value or array (not a string).
	public native bool GetString(const char[] key, char[] value, int max_size, int &size=0);

	// Checks whether a key is present in a Map.
	//
	// @param key        Key string.
	// @return           True if the key has been found, else false.
	public native bool ContainsKey(const char[] key);

	// Removes a key entry from a Map.
	//
	// @param key        Key string.
	// @return           True on success, false if the value was never set.
	public native bool Remove(const char[] key);

	// Clears all entries from a Map.
	public native void Clear();

	// Create a snapshot of the map's keys. See StringMapSnapshot.
	public native StringMapSnapshot Snapshot();

	// Retrieves the number of elements in a map.
	property int Size {
		public native get();
	}
};

/**
 * A StringMapSnapshot is created via StringMap.Snapshot(). It captures the
 * keys on a map so they can be read. Snapshots must be freed with delete or
 * CloseHandle().
 */
methodmap StringMapSnapshot < Handle
{
	// Returns the number of keys in the map snapshot.
	property int Length {
		public native get();
	}

	// Returns the buffer size required to store a given key. That is, it
	// returns the length of the key plus one.
	// 
	// @param index     Key index (starting from 0).
	// @return          Buffer size required to store the key string.
	// @error           Index out of range.
	public native int KeyBufferSize(int index);

	// Retrieves the key string of a given key in a map snapshot.
	// 
	// @param index      Key index (starting from 0).
	// @param buffer     String buffer.
	// @param maxlength  Maximum buffer length.
	// @return           Number of bytes written to the buffer.
	// @error            Index out of range.
	public native int GetKey(int index, char[] buffer, int maxlength);
};

methodmap IntMap < Handle
{
	// Creates a hash map. A hash map is a container that can map integers (called
	// "keys") to arbitrary values (cells, arrays, or strings). Keys in a hash map
	// are unique. That is, there is at most one entry in the map for a given key.
	//
	// Insertion, deletion, and lookup in a hash map are all considered to be fast
	// operations, amortized to O(1), or constant time.
	//
	// The word "Trie" in this API is historical. As of Source2Mod 1.6, tries have
	// been internally replaced with hash tables, which have O(1) insertion time
	// instead of O(n).
	//
	// The IntMap must be freed via delete or CloseHandle().
	public native IntMap();

	// Clones a hash map, returning a new handle with the same size and data.
	// This should NOT be confused with CloneHandle. This is a completely new
	// handle with the same data but no relation to the original. It should be
	// closed when no longer needed with delete or CloseHandle().
	//
	// @return              New handle to the cloned hash map
	public native IntMap Clone();

	// Sets a value in a hash map, either inserting a new entry or replacing an old one.
	//
	// @param key        Key integer.
	// @param value      Value to store at this key.
	// @param replace    If false, operation will fail if the key is already set.
	// @return           True on success, false on failure.
	public native bool SetValue(const int key, any value, bool replace=true);

	// Sets an array value in a Map, either inserting a new entry or replacing an old one.
	//
	// @param key        Key integer.
	// @param array      Array to store.
	// @param num_items  Number of items in the array.
	// @param replace    If false, operation will fail if the key is already set.
	// @return           True on success, false on failure.
	public native bool SetArray(const int key, const any[] array, int num_items, bool replace=true);

	// Sets a string value in a Map, either inserting a new entry or replacing an old one.
	//
	// @param key        Key integer.
	// @param value      String to store.
	// @param replace    If false, operation will fail if the key is already set.
	// @return           True on success, false on failure.
	public native bool SetString(const int key, const char[] value, bool replace=true);

	// Retrieves a value in a Map.
	//
	// @param key        Key integer.
	// @param value      Variable to store value.
	// @return           True on success.  False if the key is not set, or the key is set 
	//                   as an array or string (not a value).
	public native bool GetValue(const int key, any &value);

	// Retrieves an array in a Map.
	//
	// @param key        Key integer.
	// @param array      Buffer to store array.
	// @param max_size   Maximum size of array buffer.
	// @param size       Optional parameter to store the number of elements written to the buffer.
	// @return           True on success.  False if the key is not set, or the key is set 
	//                   as a value or string (not an array).
	public native bool GetArray(const int key, any[] array, int max_size, int &size=0);

	// Retrieves a string in a Map.
	//
	// @param key        Key integer.
	// @param value      Buffer to store value.
	// @param max_size   Maximum size of string buffer.
	// @param size       Optional parameter to store the number of bytes written to the buffer.
	// @return           True on success.  False if the key is not set, or the key is set 
	//                   as a value or array (not a string).
	public native bool GetString(const int key, char[] value, int max_size, int &size=0);

	// Checks whether a key is present in a Map.
	//
	// @param key        Key integer.
	// @return           True if the key has been found, else false.
	public native bool ContainsKey(const int key);

	// Removes a key entry from a Map.
	//
	// @param key        Key integer.
	// @return           True on success, false if the value was never set.
	public native bool Remove(const int key);

	// Clears all entries from a Map.
	public native void Clear();

	// Create a snapshot of the map's keys. See IntMapSnapshot.
	public native IntMapSnapshot Snapshot();

	// Retrieves the number of elements in a map.
	property int Size {
		public native get();
	}
};

methodmap IntMapSnapshot < Handle
{
	// Returns the number of keys in the map snapshot.
	property int Length {
		public native get();
	}

	// Retrieves the key integer of a given key in a map snapshot.
	// 
	// @param index      Key index (starting from 0).
	// @return           The key integer
	// @error            Index out of range.
	public native int GetKey(int index);
};
