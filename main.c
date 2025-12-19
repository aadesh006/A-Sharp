#include <stdio.h>
#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int argc, const char* argv[]) {
  initVM();
  Chunk chunk;
  initChunk(&chunk);

  // 1. Push 1.2
  int c1 = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT);
  writeChunk(&chunk, c1);

  // 2. Push 3.4
  int c2 = addConstant(&chunk, 3.4);
  writeChunk(&chunk, OP_CONSTANT);
  writeChunk(&chunk, c2);

  // 3. Add (1.2 + 3.4 -> 4.6)
  writeChunk(&chunk, OP_ADD);

  // 4. Push 5.6
  int c3 = addConstant(&chunk, 5.6);
  writeChunk(&chunk, OP_CONSTANT);
  writeChunk(&chunk, c3);

  // 5. Divide (4.6 / 5.6)
  writeChunk(&chunk, OP_DIVIDE);

  // 6. Negate the result
  writeChunk(&chunk, OP_NEGATE);

  // 7. Return (Print it)
  writeChunk(&chunk, OP_RETURN);

  interpret(&chunk);

  freeChunk(&chunk);
  freeVM();
  return 0;
}