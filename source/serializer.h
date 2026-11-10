#ifndef SERIALIZER_H
#define SERIALIZER_H

#include "common.h"
#include "filesystem.h"

//
// Memory-only serializer interface
// to read from a binary stream
//
// could be renamed / refactored later,
// but this makes flat binary reads less error-prone.
//
// Should eventually grow to support file streams as well
// in a consistent interface for read/write (though mostly read)
//

typedef struct Serializer Serializer;

enum Serializer_Type {
  SERIALIZER_TYPE_MEMORY_BUFFER,
  SERIALIZER_TYPE_FILE_HANDLE,
  SERIALIZER_TYPE_COUNT,
};

struct Serializer {
  uint8_t type;
  union {
    struct {
      uint32_t read_cursor;
      uint32_t data_length;
      uint8_t* data;
    };
    struct {
      File_Handle file_handle; 
    };
  };
};

Serializer serializer_from_memory(uint8_t* data, uint32_t data_length);
Serializer serializer_from_file_handle(File_Handle file_handle);
Serializer serializer_from_file(const char* filepath);

uint32_t serializer_readu32(Serializer* serializer);
uint16_t serializer_readu16(Serializer* serializer);
uint8_t	 serializer_readu8(Serializer* serializer);
int32_t	 serializer_readi32(Serializer* serializer);
int16_t	 serializer_readi16(Serializer* serializer);
int8_t	 serializer_readi8(Serializer* serializer);

//
// NOTE: no copy here
//
uint8_t* serializer_read_bytes(Serializer* serializer, uint32_t bytes);
void     serializer_read_into_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes);

#endif
