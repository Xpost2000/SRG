#include "memory_card.h"
#include <psxgpu.h>
#include <psxapi.h>

// For handling mc status
#define IDE_PROCESSING 0x01
#define READ_PROCESSING 0x01
#define WRITE_PROCESSING 0x04
#define TEST_PROCESSING 0x08
#define TIMEOUT 0x11
#define ERROR 0x21

#define HEADER_DOCUMENT_NAME ("SRG - PSX HBRW MECHA")
#define SAVFILE_NAME_PREFIX  ("buX0:BASCUS-00000SRGSAV") 

// TODO(jerry):
// might want to open this to be more flexible, but it's
// not a big deal imo.
#define ICOFILE_PATHNAME ("\\RES\\SAVICO.TIM")       

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

static Memory_Card_Events g_memcard_events = {};

static void _enable_memcard_events(void)
{
  EnableEvent(g_memcard_events.write_complete);
  EnableEvent(g_memcard_events.new_card);
  EnableEvent(g_memcard_events.timed_out);
  EnableEvent(g_memcard_events.general_error);
}

static void _disable_memcard_events(void)
{
  DisableEvent(g_memcard_events.write_complete);
  DisableEvent(g_memcard_events.new_card);
  DisableEvent(g_memcard_events.timed_out);
  DisableEvent(g_memcard_events.general_error);
}

static int _wait_memcard_event(void)
{
  int received_event = 0;
  int result = MEMCARD_EVENT_RESPONSE_NONE;

  while (!received_event) {
    if (TestEvent(g_memcard_events.new_card)==1) {
      _debugprintf("[MEMORY-CARD]: Detected uninitialized memory card.");
      received_event = 1;
      result = MEMCARD_EVENT_RESPONSE_NEWCARD;
    }

    if (TestEvent(g_memcard_events.write_complete)==1) {
      _debugprintf("[MEMORY-CARD]: Write complete, this means the card exists.");
      received_event = 1;
      result = MEMCARD_EVENT_RESPONSE_SUCCESSFUL;
    }

    if (TestEvent(g_memcard_events.timed_out)==1) {
      _debugprintf("[MEMORY-CARD]: Error event (timeout) received.");
      received_event = 1;
      result = MEMCARD_EVENT_RESPONSE_TIMEOUT;
    }

    if (TestEvent(g_memcard_events.general_error)==1) {
      _debugprintf("[MEMORY-CARD]: Error event received.");
      received_event = 1;
      result = MEMCARD_EVENT_RESPONSE_ERROR;
    }
  }

  return result;
}

/*
 * NOTE(jerry):
 * EventClass table
 *
 * SOURCE_DESCRIPTOR / EVENT CLASS
 *
 * SwCARD / EvSpIOE    -- connected
 * SwCARD / EvSpTIMOUT -- Not connected
 */
int memory_card_port_connected(int port)
{
  int result = 0;
  int portmask = port << 4;
  int status_flag_type;
  assert((port == 0 || port == 1) && "[MEMORY-CARD] unknown card port number specified.");

  // NOTE(jerry):
  // While games seem to do this asynchronously, we're just going to block since that
  // requires more hooking else where...
  // (FF7 for example appears to read asynchronously.)

  _enable_memcard_events();

  {
    // NOTE(jerry): *should* be in critical section
    // in-case any IRQs hit it, though I do not believe there are any issues
    // with this, since mostly everything should be disabled in a memcard save scree anyway.
    _card_info(portmask);
    status_flag_type = _wait_memcard_event();
    switch (status_flag_type) {
      case MEMCARD_EVENT_RESPONSE_NEWCARD: {
        _card_clear(portmask);
        status_flag_type = _wait_memcard_event();

        if ((status_flag_type == MEMCARD_EVENT_RESPONSE_ERROR) ||
            (status_flag_type == MEMCARD_EVENT_RESPONSE_TIMEOUT)) {
          _debugprintf("[MEMORY-CARD] card was possibly removed or other communication error. Not retrying.");
          result = 0;
          goto bye;
        }
      } break;
      case MEMCARD_EVENT_RESPONSE_TIMEOUT:
      case MEMCARD_EVENT_RESPONSE_ERROR: {
        _debugprintf("[MEMORY-CARD] error found, not retrying.");
        result = 0;
        goto bye;
      } break;
      case MEMCARD_EVENT_RESPONSE_SUCCESSFUL: {
        result = 1;
        _debugprintf("[MEMORY-CARD] Existence confirmed.");
        goto bye;
      } break;
    }

    _card_load(port << 4);
    status_flag_type = _wait_memcard_event();

    switch (status_flag_type) {
      case MEMCARD_EVENT_RESPONSE_NEWCARD: {
        _debugprintf("[MEMORY-CARD] unformatted memory card found. Suggesting formatting.");
        result = 2;
        goto bye;
      } break;
      case MEMCARD_EVENT_RESPONSE_SUCCESSFUL: {
        result = 1;
        _debugprintf("[MEMORY-CARD] Existence confirmed.");
        goto bye;
      } break;
      case MEMCARD_EVENT_RESPONSE_TIMEOUT:
      case MEMCARD_EVENT_RESPONSE_ERROR: {
        _debugprintf("[MEMORY-CARD] error found, not retrying.");
        result = 0;
        goto bye;
      } break;
    }
  }
bye:
  _disable_memcard_events();
  return result;
}

void memory_card_format(int port)
{
#if 0
  //
  // NOTE(jerry): I can't remember the source of when we found
  // out that the _card_format call does nothing.
  //

  //
  // Here semantically, this thing is apparently not real!
  //
  int portmask = port << 4;
  _card_format(portmask); // NOTE(jerry): function is synchronous.
#endif
}

char* get_game_save_name(int port, int id_slot)
{
  // NOTE(jerry):
  // I am seeming to not trust psn00bsdk with snprintf,
  // assume we have 15 possible slots.
  static char result[128] = {};
  strncpy(result, SAVFILE_NAME_PREFIX, sizeof(result));

  switch (port) {
    case 0: result[2] = '0'; break;
    case 1: result[2] = '1'; break;
    default:
      assert(0 && "[SAVE] invalid port id specified");
  }

  result[(sizeof(SAVFILE_NAME_PREFIX)-1)]   = '0' + id_slot/10;
  result[(sizeof(SAVFILE_NAME_PREFIX)-1)+1] = '0' + id_slot%10;
  result[(sizeof(SAVFILE_NAME_PREFIX)-1)+2] = 0;
  _debugprintf("[SAVE] requested savename: %s\n", result);
  return result;
}

static SIE_MemoryCard_Header build_memory_card_header(TIM_IMAGE* icon_image, char* document_name, int blockcount)
{
  SIE_MemoryCard_Header result = {};
  assert((icon_image->mode & 0x3) == 0 && "[MEMORY-CARD] Icon image provided is not 4bpp");
  assert(blockcount <= MEMCARD_MAX_BLOCKS && "[MEMORY-CARD] block count is higher than standard ps1 block count.");

  {
    result.magic[0] = 'S';
    result.magic[1] = 'C';

    result.type = 0x11; // single icon type.
    result.blockcount = blockcount;
    {
      int copycount = strlen(document_name);
      if (copycount > sizeof(result.document_name)) {
        copycount = sizeof(result.document_name);
      }
      strncpy(result.document_name, document_name, copycount);
    }

    memcpy(result.ico_clut, (uint8_t*)icon_image->caddr, 32);
    memcpy(result.ico0, (uint8_t*)icon_image->paddr, 128);
  }

  return result;
}

static SIE_MemoryCard_Header read_memory_card_header(int fd)
{
  SIE_MemoryCard_Header result = {};
  int readcount = read(fd, &result, sizeof(result));
  assert(readcount == sizeof(result) && "[MEMORY-CARD] short count on header read.");
  return result;
}

typedef union
{
    uint32_t words[2];
    uint64_t qword;
} QWORD;

void memory_card_initialize(void)
{
    _debugprintf("[MEMORY-CARD] Init");
    InitCARD(1);

    g_memcard_events.write_complete = OpenEvent(SwCARD, EvSpIOE,    EvMdNOINTR, NULL);
    g_memcard_events.new_card       = OpenEvent(SwCARD, EvSpNEW,    EvMdNOINTR, NULL);
    g_memcard_events.timed_out      = OpenEvent(SwCARD, EvSpTIMOUT, EvMdNOINTR, NULL);
    g_memcard_events.general_error  = OpenEvent(SwCARD, EvSpERROR,  EvMdNOINTR, NULL);
}

void memory_card_start(void)
{
    _debugprintf("[MEMORY-CARD] Start");
    StartCARD();
    _bu_init();
}

// NOTE (Gabe): This is bad, but works since idk why I can get
//  openevent handler to work, copy example code but could not
//  get it to work.
int memory_card_file_exists(char* file)
{
  assert(
    file[0] == 'b' &&
    file[1] == 'u' &&
    (file[2] == '0' || file[2] == '1') &&
    file[3] == '0' && // NOTE(jerry): is technically the extension connector number, but a standard card is 0 (which is 90% of the audience anyway.)
    file[4] == ':' &&
    "[MEMORY-CARD] file name is not prefixed with buX0:, hard fail."
  );
  _debugprintf("[MEMORY-CARD] Check card");

  int fd;
  if ((fd = open(file, FREAD)) == -1)
  {
    _debugprintf("[MEMORY-CARD] Card does not exist");
    return 0;
  }

  _debugprintf("[MEMORY-CARD] Card exist");
  close(fd);
  return 1;
}

static void serialize_card(const SIE_MemoryCard_Header* const header, void* data, size_t data_size, int fd)
{
  int write_count = 0;
  write_count += write(fd, header, sizeof(*header));
  write_count += write(fd, data, data_size);

  // NOTE (Gabe): If write call size is not multi of 128, something is wrong
  _debugprintf("[MEMORY-CARD] write call: %d bytes (%d blocks)", write_count, header->blockcount);
  assert(((write_count % 128) == 0) && "[MEMORY-CARD] write call failed");
  assert(((write_count % MEMCARD_BLOCK_SZ) == 0) && "[MEMORY-CARD] failure to write full block");
  assert(((write_count == header->blockcount * MEMCARD_BLOCK_SZ)) && "[MEMORY-CARD] wrote invalid # of blocks");
}

static void deserialize_card(void* data, size_t data_size, int fd)
{
  SIE_MemoryCard_Header header;

  int read_count = 0;
  read_count += read(fd, &header, sizeof(header));
  read_count += read(fd, data, data_size);

  // Validate header...
  {
    assert(header.magic[0] == 'S' && header.magic[1] == 'C' && "[MEMORY-CARD] invalid memory card header magic.");
    // game check.
    assert(strcmp(header.document_name, HEADER_DOCUMENT_NAME) == 0 && "[MEMORY-CARD] invalid game document name.");
  }

  // NOTE (Gabe): If read call size is not a multi of 128, something is wrong
  _debugprintf("[MEMORY-CARD] read call: %d bytes (%d blocks) vs. %d", read_count, header.blockcount, MEMCARD_BLOCK_SZ);
  assert(((read_count % 128) == 0) && "[MEMORY-CARD] read call failed");
  assert(((read_count % MEMCARD_BLOCK_SZ) == 0) && "[MEMORY-CARD] failure to read full block");
  assert(((read_count == header.blockcount * MEMCARD_BLOCK_SZ)) && "[MEMORY-CARD] read invalid # of blocks");
}

int memory_card_write(char* savefile_name, void* icon_as_tim, void* data, size_t data_size)
{
    _debugprintf("[MEMORY-CARD] Write out game state to card");
    TIM_IMAGE imghdr = {};
    int slot_count = ((sizeof(SIE_MemoryCard_Header) + data_size) + 8191) / MEMCARD_BLOCK_SZ;
    int is_card_good = memory_card_file_exists(savefile_name);
    int fd;

    if (!is_card_good) {
      _debugprintf("[MEMORY-CARD] card file not good, trying to make file?");
      fd = open(savefile_name, FCREATE | (slot_count << 16));
    } else {
      _debugprintf("[MEMORY-CARD] card file good, open in write.");
      fd = open(savefile_name, FWRITE);
    }

    if (fd == -1) {
        _debugprintf("[MEMORY-CARD] Failed to write");
        return 0; // failed
    }

    GetTimInfo((uint32_t*) icon_as_tim, &imghdr);
    {
      SIE_MemoryCard_Header memcard_header =
	build_memory_card_header(&imghdr,
				 HEADER_DOCUMENT_NAME,
				 slot_count);

      serialize_card(&memcard_header, data, data_size, fd);
    }

    close(fd);
    return 1; // card saved
}

int memory_card_read(char* savefile_name, void* data, size_t data_size)
{
    _debugprintf("[MEMORY-CARD] Read out game state to card");

    int is_card_good = memory_card_file_exists(savefile_name);
    if (!is_card_good)
    {
        return 0;
    }

    int fd;
    if ((fd = open(savefile_name, FREAD)) == -1)
    {
        _debugprintf("[MEMORY-CARD] Failed to read");
        return 0; // failed
    }

    deserialize_card(data, data_size, fd);
    close(fd);
    return 1; // was able to read
}

void memory_card_end(void)
{
    _debugprintf("[MEMORY-CARD] Stop");
    StopCARD();
}

uint64_t djb2_checksum(const void const *data, int size)
{
    // djb2 from: https://www.cse.yorku.ca/~oz/hash.html
    const char *bytes = (const char *)data;
    uint64_t hash = 5381;

    for (int i = 0; i < size; i++)
    {
        hash = ((hash << 5) + hash) + bytes[i];
    }

    return hash;
}

// Print out uint64_t as two uint32_t
void debug_print_uint64_t(uint64_t x)
{
    QWORD qword = (QWORD){.qword = x};
    printf("[0] = %x, [1] = %x\n", qword.words[0], qword.words[1]);
}
