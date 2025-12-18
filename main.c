#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, const char* argv[]){
    Chunk chunk;
    initChunk(&chunk);

    // --- TEST CASE 1: Add a constant ---
  // 1. Add the number 1.2 to the pool. Get index 0 back.
  int index = addConstant(&chunk, 1.2);

  // 2. Write the instruction [OP_CONSTANT]
  writeChunk(&chunk, OP_CONSTANT);

  // 3. Write the address [0]
  writeChunk(&chunk, index);

  // --- TEST CASE 2: Return ---
  writeChunk(&chunk, OP_RETURN);

  // --- DISASSEMBLE ---
  disassembleChunk(&chunk, "test chunk");

  freeChunk(&chunk);
  return 0;
}