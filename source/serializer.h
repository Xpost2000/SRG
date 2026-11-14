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

enum Serializer_Mode {
  SERIALIZER_MODE_READ = 0,
  SERIALIZER_MODE_WRITE = 1,
};

struct Serializer {
  uint8_t type;

  //
  // Primary serializer mode for
  // the _serialize API.
  //
  // It doesn't actually enforce anything,
  // it's just for making usage more convenient.
  //

  uint8_t mode;
  union {
    struct {
      uint32_t cursor;
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
void       serializer_set_mode(Serializer* serializer, uint8_t mode);

uint32_t serializer_readu32(Serializer* serializer);
uint16_t serializer_readu16(Serializer* serializer);
uint8_t	 serializer_readu8(Serializer* serializer);
int32_t	 serializer_readi32(Serializer* serializer);
int16_t	 serializer_readi16(Serializer* serializer);
int8_t	 serializer_readi8(Serializer* serializer);

void     serializer_readu32_inplace(Serializer* serializer, uint32_t* x);
void     serializer_readu16_inplace(Serializer* serializer, uint16_t* x);
void     serializer_readu8_inplace(Serializer* serializer,  uint8_t*  x);
void     serializer_readi32_inplace(Serializer* serializer, int32_t*  x);
void     serializer_readi16_inplace(Serializer* serializer, int16_t*  x);
void     serializer_readi8_inplace(Serializer* serializer,  int8_t*   x);

void     serializer_writeu32(Serializer* serializer, uint32_t* x);
void     serializer_writeu16(Serializer* serializer, uint16_t* x);
void     serializer_writeu8(Serializer* serializer,  uint8_t*  x);
void     serializer_writei32(Serializer* serializer, int32_t*  x);
void     serializer_writei16(Serializer* serializer, int16_t*  x);
void     serializer_writei8(Serializer* serializer,  int8_t*   x);

void     serializer_serializeu32(Serializer* serializer, uint32_t* x);
void     serializer_serializeu16(Serializer* serializer, uint16_t* x);
void     serializer_serializeu8(Serializer* serializer,  uint8_t*  x);
void     serializer_serializei32(Serializer* serializer, int32_t*  x);
void     serializer_serializei16(Serializer* serializer, int16_t*  x);
void     serializer_serializei8(Serializer* serializer,  int8_t*   x);

//
// NOTE: no copy here
//
uint8_t* serializer_read_bytes(Serializer* serializer, uint32_t bytes);
void     serializer_write_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes);
void     serializer_read_into_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes);
void     serializer_serialize_bytes(Serializer* serializer, uint8_t* dest, uint32_t bytes);

#endif
