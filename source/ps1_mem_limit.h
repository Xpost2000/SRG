#ifndef PS1_MEM_LIMIT_H
#define PS1_MEM_LIMIT_H

#include <stddef.h>

//
// This is a highly platform specific thing
// that doesn't belong really much else where imo.
//
// Since the other stuff *could* be more platform generic
// eventually.
//
// This could've been in the memory_arena module but I don't
// think it makes sense to be there.
//

size_t system_get_remaining_allocatable_memory(void);

#endif
