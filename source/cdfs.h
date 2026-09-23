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

#define CD_SECTOR_SIZE (2048)
#define CD_MAX_CACHED_FILES (128)
#define CD_READ_BUFFER_CACHE_SZ (CD_SECTOR_SIZE * 336) 

#include <psxcd.h>
#include <psxsio.h>

typedef struct CD_File CD_File;
typedef struct CD_File_Buffer CD_File_Buffer;

struct CD_File {
  CdlFILE file_index;
  int     valid;
  size_t  cursor;
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

CD_File		cd_file_open(const char* fpath);
size_t		cd_file_get_size(CD_File* file);
size_t		cd_file_get_memory_required(CD_File* file);
size_t		cd_file_read_sync_uncached(CD_File* file, unsigned char* buffer, size_t size);
int		cd_is_ready_to_receive_read(int blocking);
CD_File_Buffer	cd_file_read_uncached(Memory_Arena* arena, CD_File* file);

#endif
