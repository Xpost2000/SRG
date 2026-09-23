#ifndef MEMORY_CARD_H
#define MEMORY_CARD_H

//
// This is the low level memory card system.
//

/**
 * Basic system for saving game state to PS1 memory card.
 *
 * NOTE:
 * - All saved data is saved to port 1, as _card_load
 *  isn't working correctly for some reason.
 *
 * - Make sure after saving game or reading, that you
 *  start the gamepad again, as for some reason the PS1
 *  stops the gamepad when MC is running.
 *
 * TODO:
 * - [ ] Need to switch / figure openevent system
 * - [ ] Need to get _card_load working to save to different
 *  MC ports on PS1
 */

#include "common.h"

//
// NOTE (Gabe): All data written to MC must be a multi of 128
//
#define MEMCARD_SECTOR_SZ  (128)  // 128B
#define MEMCARD_BLOCK_SZ   (8192) // 8KB
#define MEMCARD_MAX_BLOCKS (15)

// For handling mc status
#define IDE_PROCESSING 0x01
#define READ_PROCESSING 0x01
#define WRITE_PROCESSING 0x04
#define TEST_PROCESSING 0x08
#define TIMEOUT 0x11
#define ERROR 0x21

typedef struct SIE_MemoryCard_Header SIE_MemoryCard_Header;
typedef struct Memory_Card_Events Memory_Card_Events;

struct Memory_Card_Events {
  int write_complete;
  int new_card;
  int timed_out;
  int general_error;
};

enum Memory_Card_Event_Response {
  MEMCARD_EVENT_RESPONSE_NONE,
  MEMCARD_EVENT_RESPONSE_SUCCESSFUL,
  MEMCARD_EVENT_RESPONSE_NEWCARD,
  MEMCARD_EVENT_RESPONSE_TIMEOUT,
  MEMCARD_EVENT_RESPONSE_ERROR,
};

struct SIE_MemoryCard_Header {  // NOTE(jerry): Non-PDA compatible. This memory card is not an application.
                                // This is 512 bytes or 4 sectors.
  char     magic[2];
  uint8_t  type; 
  uint8_t  blockcount;
  char     document_name[64];   // encoded as Shift-JIS, which has ASCII has a subset, so no conversion needed.
                                // unless we want a fancy japanese name.
  char     _pad0[28];           // this is the PDA data, but for non-pdas MUST be zeroed.
  uint8_t  ico_clut[32];
  uint8_t  ico0[128];           //  main icon. 16x16 4bpp (16 bit color)
  uint8_t  ico1[128];           //  animation frame 1, invalid if Type != 0x11
  uint8_t  ico2[128];           //  animation frame 2
};

//
// NOTE(jerry): returned as a local
// static string. Should not be stored for any period of time.
//
char* get_game_save_name(int port, int id_slot);

void  memory_card_initialize(void);

//
// NOTE(jerry): when this happens, the gamepad module
// is turned off. Need to re-enable when it's over.
//
void  memory_card_start(void);

//
// NOTE(jerry): remember to start/stop card
//
void memory_card_format(int port);

// ret 1 == okay, ret 2 == okay, but format card. ret 0 == bad.
int memory_card_port_connected(int port);
int memory_card_file_exists(char* savefile_name);

//
// The payloads should be padded to required multiple size.
//
int memory_card_write(char* savefile_name, void* icon_as_tim, void* data, size_t data_size);
int memory_card_read(char* savefile_name, void* data, size_t data_size);
void memory_card_stop(void);

void debug_print_uint64_t(uint64_t x);
#endif
