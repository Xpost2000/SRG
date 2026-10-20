#ifndef FILESYSTEM_H
#define FILESYSTEM_H

//
// Simple file system API on top of cdfs
// fopen/fread/fclose-style interface
// No seeking (to be implemented later)
//

#include "common.h"
#include "cdfs.h"

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
#define MAX_OPENED_FILES (2)

File_Handle filesystem_open(const char* filepath);
int         filesystem_read(File_Handle file, unsigned char* data, size_t size);
size_t      filesystem_get_file_size(File_Handle file);
void        filesystem_close(File_Handle file);

#endif
