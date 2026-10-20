#ifndef CDFS_H
#define CDFS_H

//
// Uncached raw CD file system helper
// functions, caches have to be implemented on top of this,
//
// I also got rid of all the async stuff as it's... very complicated
// to be honest, and it's only async so far as the current sector read is
// asynchronous.
//
// Which is not very parallelizable, so I don't really think it's worth keeping.
//

#include "common.h"
#include "memory_arena.h"
#include <psxcd.h>
#include <psxsio.h>

#define CD_SECTOR_SIZE (2048)
#define CD_MAX_CACHED_FILES (128)
#define CD_READ_BUFFER_CACHE_SZ (CD_SECTOR_SIZE * 336) 

typedef struct CD_File CD_File;
typedef struct CD_File_Buffer CD_File_Buffer;

struct CD_File {
  CdlFILE file_index;
  CdlLOC  start;
  CdlLOC  end;
  int     valid;
};

//
// NOTE(jerry):
// NEVER store these, since these are cached items that could
// expire at any moment!
//
// The CD_File is safe to store, and just re-read each time
// you want one of these. Never store them. They are temporary.
// CD_File is forever.
//
struct CD_File_Buffer {
  uint8_t* memory;
  size_t   length;
};

enum CD_File_Seek_Where {
  CD_FILE_SEEK_WHERE_BEGIN,
  CD_FILE_SEEK_WHERE_END,
  CD_FILE_SEEK_WHERE_COUNT,
};

void            cd_start(void);
CD_File		cd_file_open(const char* fpath);
size_t		cd_file_get_size(CD_File* file);
size_t          cd_file_get_sector_total(CD_File* file);
size_t		cd_file_get_memory_required(CD_File* file);
size_t          cd_file_get_aligned_size(size_t size);
void            cd_file_seek(CD_File* file, uint8_t where, uint32_t sector_offset);
//
// Because this is a low level module
// this only works if it reads in CD_SECTOR_SIZEs
//
// a higher level wrapper needs to be written to support non sector sizes.
//
size_t		cd_file_read_sync_uncached(CD_File* file, unsigned char* buffer, size_t size);
int		cd_is_ready_to_receive_read(int blocking);
CD_File_Buffer	cd_file_read_uncached(Memory_Arena* arena, CD_File* file);

#endif
