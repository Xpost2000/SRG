#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "common.h"
#include "cdfs.h"

#define MAX_OPENED_FILES (2)

//
// Interface
//

typedef unsigned int File_Handle;

//
// Files are generally needed exclusively
// for load-time, so there shouldn't *actually*
// be more than one active file at a time for buffer usage.
//
// So all files need to essentially be opened and loaded sequentially
//

enum File_System_Seek_Where {
  FILE_SYSTEM_SEEK_WHERE_START,
  FILE_SYSTEM_SEEK_WHERE_END,
  FILE_SYSTEM_SEEK_WHERE_COUNT,
};

File_Handle filesystem_open(const char* filepath);
int         filesystem_read(File_Handle file, unsigned char* data, size_t size);
void        filesystem_seek(File_Handle file, uint8_t where, int32_t offset);
size_t      filesystem_get_file_size(File_Handle file);
void        filesystem_close(File_Handle file);

#endif
