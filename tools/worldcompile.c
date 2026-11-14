//
// This compiles the V1 text format for SRG worlds
// into the binary format to be consumed at runtime.
//

#include "common.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
  if (argc < 2) {
    printf("Usage:\n\t%s <file0.map> ... <file1.map>\n", argv[0]);
    printf("worldcompile will output files file0.slvl ... file1.slvl which are the\n");
    printf("binary format for SRG levels.\n");
    return 0;
  }

  return 0; 
}
