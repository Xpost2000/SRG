//
// This compiles the V1 text format for SRG worlds
// into the binary format to be consumed at runtime.
//
// The text format isn't being used for any reason so
// it doesn't "really exist outside of that"...
//

#include "common.h"
#include "world.h"
#include "world_builder.h"
#include "memory_arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int            tile_count = 0;
int            world_width;
int            world_height;
uint8_t*       tile_data;
uint8_t        memory[Kilobyte(256)];
Memory_Arena*  arena;
World_Builder* world_builder;

//ascii to unsigned
static uint32_t atou(char* s)
{
  uint32_t l;
  l = strtoul(s, 0, 0);
  return l;
}

static int32_t atof_20_12(char* s)
{
  return (floorf(atof(s) * 4096));
}

static int32_t atoang4096(char* s)
{
  return floorf((atoi(s)/360.0f) * 4096);
}

//
// NOTE(jerry):
// maybe we should consider having a proper
// tokenizer thing, but the game engine (if we can help it)
// should avoid having to directly do any text (I know mml.c/mml.h handle
// text, and arguably that should also have a compiler but eh)
//
static char* nexttoken(char* s, int* len)
{
  static char token[4096];
  int         i;
  int         w;
  char        ch;

  i = 0;
  w = 0;
  memset(token, 0, sizeof(token));

  while (s[i] == ' ' || s[i] == '\t') {
    i++;
  }

  while (1) {
    ch = s[i];

    if (ch == 0) {
      return NULL; 
    }

    if (ch == '\"') {
      i++;

      //
      // read a string
      //
      {
        while (1) {
          ch = s[i];
          if (ch == '\"') {
            i++;
            break;
          } else {
            token[w++] = ch;
            i++;
          }
        }
      }
    } else if  (ch == ' ' || ch == '\t') {
      break;
    } else {
      token[w++] = ch;
      i++;
    } 
  }

  *len = i;
  return token;
}

static char* readline(FILE* f)
{
  static char linebuffer[4096];
  int         ch;
  int         i;

  i = 0;
  memset(linebuffer, 0, sizeof(linebuffer));
  while (1) {
    ch = fgetc(f);
    if (ch == EOF) {
      return NULL;
    }

    if (ch == '\n' || ch == '0') {
      break;
    }

    linebuffer[i] = (char) ch;
    i++;
  }

  return linebuffer;
}

void read_tiles(FILE* f)
{
  char* line;
  int   max_x=0;
  int   lines;
  int   i;
  int   s;

  lines = 0;
  _debugprintf("reading tiles");
  while ((line = readline(f))) {
    if (strcmp(line, "tiles_end") == 0) {
      break;
    }

    s = strlen(line);
    max_x = max(max_x, s);
    for (i = 0; i < s; ++i) {
      tile_data[tile_count++] = line[i]; 
    }

    lines++;
  }

  world_width = max_x;
  world_height = lines;
  world_builder_set_world_tile_data(world_builder, world_width, world_height, tile_data);
  _debugprintf("World dimensions: %d x %d", world_width, world_height);
}

void read_entities(FILE* f)
{
  char* line;
  char* tok;
  int   length;

  _debugprintf("reading entities");
  while ((line = readline(f))) {
    if (line[0] == '/' && line[1] == '/') {
      continue; 
    }

    if (line[0] == '\n' || line[0] == 0) {
      continue;
    }

    if (strcmp(line, "entities_end") == 0) {
      break;
    }

    tok = nexttoken(line, &length);
    line += length;

    if (strcmp(tok, "player_spawn") == 0) {
      int32_t x;
      int32_t y;
      int16_t look_angle;

      tok = nexttoken(line, &length);
      line += length;
      x = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      y = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      look_angle = atoang4096(tok);

      _debugprintf("player_spawn: %d, %d, %d", x, y, look_angle);
      world_builder_add_player_spawn(world_builder, x, y, look_angle);
    } else if (strcmp(tok, "end_stage_region") == 0) {
      int32_t x;
      int32_t y;
      int32_t w;
      int32_t h;
      int32_t next_level_id;

      tok = nexttoken(line, &length);
      line += length;
      x = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      y = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      w  = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      h  = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      next_level_id = atoi(tok);
      
      _debugprintf("end_stage_region: %d, %d, %d, %d", x, y, w, h, next_level_id);
      world_builder_add_stage_end_region(world_builder, x, y, w, h, next_level_id);
    } else if (strcmp(tok, "object_prop") == 0) {
      int32_t  x;
      int32_t  y;
      int32_t  asset_id;
      uint32_t flags;
      int16_t  look_angle;

      tok = nexttoken(line, &length);
      line += length;
      x = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      y = atof_20_12(tok);

      tok = nexttoken(line, &length);
      line += length;
      asset_id  = atoi(tok);

      tok = nexttoken(line, &length);
      line += length;
      flags  = atou(tok);

      tok = nexttoken(line, &length);
      line += length;
      look_angle = atoang4096(tok);
      
      _debugprintf("object_prop: %d, %d, %d, %d, %d", x, y, asset_id, flags, look_angle);
      world_builder_add_object_prop(world_builder, x, y, asset_id, flags, look_angle);
    }
  }
}

static void process_map_file(FILE* f)
{
  char* line;

  _debugprintf("processing map file.");
  while ((line = readline(f))) {
    if (line[0] == '/' && line[1] == '/') {
      continue; 
    }

    if (strcmp(line, "tiles_begin") == 0) {
      read_tiles(f);
    } else if (strcmp(line, "entities_begin") == 0) {
      read_entities(f);
    }
  }
}

//
// static string scoped
// to the length
//
static char* remove_suffix(const char* s)
{
  static char _buff[Kilobyte(4)];
  int i;
  int slen;
  int dot;

  slen = strlen(s);
  for (i = (slen-1); i >= 0; --i) {
    if (s[i] == '.') {
      dot = i;
      break;
    }
  }

  //
  // no bounds checking here
  //
  for (i = 0; i < dot; ++i) {
    _buff[i] = s[i];
  }
  _buff[i] = 0;

  return _buff;
}

int main(int argc, char** argv)
{
  int i;
  FILE* f;
  char  filebufname[4096];

  if (argc < 2) {
    printf("Usage:\n\t%s <file0.map> ... <file1.map>\n", argv[0]);
    printf("worldcompile will output files file0.slvl ... file1.slvl which are the\n");
    printf("binary format for SRG levels.\n");
    return 0;
  }

  arena         = memory_arena_create("global-arena", memory, sizeof(memory));
  tile_data     = memory_arena_push_unaligned(arena, Kilobyte(16));
  world_builder = world_builder_create(arena);

  _debugprintf("processing (%d) file(s)", argc-1);
  for (i = 1; i < argc; ++i) {
    snprintf(filebufname, sizeof(filebufname), "%s.slvl", remove_suffix(argv[i]));

    f = fopen(argv[i], "rb+");
    process_map_file(f);

    fclose(f);
  }

  return 0; 
}
