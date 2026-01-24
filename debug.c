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

static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  printf("%-16s %4d -> %d\n", name, offset,
         offset + 3 + sign * jump);
  return offset + 3;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2; 
}

// The Main Decoder Switch
int disassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset); // Print the memory address (e.g., 0000)

  uint8_t instruction = chunk->code[offset];
  switch (instruction) {
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);

    case OP_ADD:
      return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:
      return simpleInstruction("OP_DIVIDE", offset);
    case OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);

    case OP_POP:
      return simpleInstruction("OP_POP", offset);
    case OP_PRINT:
      return simpleInstruction("OP_PRINT", offset);
    
    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);

    case OP_EQUAL:   return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER: return simpleInstruction("OP_GREATER", offset);
    case OP_LESS:    return simpleInstruction("OP_LESS", offset);
    case OP_NOT:     return simpleInstruction("OP_NOT", offset);

    case OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);

    case OP_GET_LOCAL:
      return constantInstruction("OP_GET_LOCAL", chunk, offset); // Note: It uses a byte operand
    case OP_SET_LOCAL:
      return constantInstruction("OP_SET_LOCAL", chunk, offset);

    case OP_CALL:
      return byteInstruction("OP_CALL", chunk, offset);

    case OP_JUMP:
      return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);

    case OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, chunk, offset);

    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    default:
      printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}