#ifndef asharp_vm_h
#define asharp_vm_h

#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"
#include "common.h"

//Starting with a fixed-size stack
//#define STACK_MAX 256
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots; // Pointer to the start of this frame's stack window
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;

  Value stack[STACK_MAX];
  Value* stackTop;
  Table globals;
  Table strings;
  
  size_t bytesAllocated;
  size_t nextGC;
  
  Obj* objects;
} VM;

typedef enum{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
}InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

extern VM vm; 

#endif