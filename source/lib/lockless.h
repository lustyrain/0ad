#include "posix_types.h"	// uintptr_t

// atomic "compare and swap". compare the machine word at <location> against
// <expected>; if not equal, return false; otherwise, overwrite it with
// <new_value> and return true.
extern bool CAS_(uintptr_t* location, uintptr_t expected, uintptr_t new_value);

#define CAS(l,o,n) CAS_((uintptr_t*)l, (uintptr_t)o, (uintptr_t)n)


//
// lock-free singly linked list
//

struct LFList
{
	void* head;
};

// make ready a previously unused(!) list object. if a negative error
// code (currently only ERR_NO_MEM) is returned, the list can't be used.
extern int lfl_init(LFList* list);

// call when list is no longer needed; should no longer hold any references.
extern void lfl_free(LFList* list);

// return pointer to "user data" attached to <key>,
// or 0 if not found in the list.
extern void* lfl_find(LFList* list, void* key);

// insert into list in order of increasing key. ensures items are unique
// by first checking if already in the list. returns 0 if out of memory,
// otherwise a pointer to "user data" attached to <key>. the optional
// <was_inserted> return variable indicates whether <key> was added.
extern void* lfl_insert(LFList* list, void* key, size_t additional_bytes, int* was_inserted);

// remove from list; return -1 if not found, or 0 on success.
extern int lfl_erase(LFList* list, void* key);


//
// lock-free hash table (chained, fixed size)
//

struct LFHash
{
	LFList* tbl;
	uint mask;
};

// make ready a previously unused(!) hash object. table size will be
// <num_entries>; this cannot currently be expanded. if a negative error
// code (currently only ERR_NO_MEM) is returned, the hash can't be used.
extern int lfh_init(LFHash* hash, size_t num_entries);

// call when hash is no longer needed; should no longer hold any references.
extern void lfh_free(LFHash* hash);

// return pointer to "user data" attached to <key>,
// or 0 if not found in the hash.
extern void* lfh_find(LFHash* hash, uintptr_t key);

// insert into hash if not already present. returns 0 if out of memory,
// otherwise a pointer to "user data" attached to <key>. the optional
// <was_inserted> return variable indicates whether <key> was added.
extern void* lfh_insert(LFHash* hash, uintptr_t key, size_t additional_bytes, int* was_inserted);

// remove from hash; return -1 if not found, or 0 on success.
extern int lfh_erase(LFHash* hash, uintptr_t key);
