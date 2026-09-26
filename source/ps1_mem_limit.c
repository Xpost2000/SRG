#include "ps1_mem_limit.h"

//
// This symbol is defined in PSN00BSDK's linker script
//
extern char		_end[];

//
// Kernel is mapped at 0x80200000, which is where memory
// starts. Add 2MiB (0x00200000) on top of that
//
static const char*	g_memory_hi_address = (char*)0x80200000;

size_t system_get_remaining_allocatable_memory(void)
{
  return g_memory_hi_address - (char*) &_end;
}
