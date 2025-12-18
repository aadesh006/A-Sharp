#include <stdio.h>
#include "debug.h"
#include "value.h"


void disassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    // We let the function return the NEW offset.
    // Why? Because instructions are different sizes!
    // OP_RETURN is 1 byte. OP_CONSTANT is 2 bytes.
    offset = disassembleInstruction(chunk, offset);
  }
}

// -----------------------------------------------------------
// Helper: Prints simple instructions (like OP_RETURN)
// -----------------------------------------------------------
static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1; // Move past the opcode (1 byte total)
}

// -----------------------------------------------------------
// Helper: Prints instructions with data (like OP_CONSTANT)
// -----------------------------------------------------------
static int constantInstruction(const char* name, Chunk* chunk, int offset) {
  // 1. Read the operand (The Index/Ticket)
  // It is located immediately after the opcode.
  uint8_t constantIndex = chunk->code[offset + 1];

  // 2. Print the Opcode name and the Index
  printf("%-16s %4d '", name, constantIndex);
  
  // 3. Look up the actual value in the pool and print it
  printValue(chunk->constants.values[constantIndex]);
  printf("'\n");
  
  // 4. Return the new offset
  // We skipped: [OP_CONSTANT] [INDEX] -> 2 bytes total.
  return offset + 2; 
}

// -----------------------------------------------------------
// The Main Decoder Switch
// -----------------------------------------------------------
int disassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset); // Print the memory address (e.g., 0000)

  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}