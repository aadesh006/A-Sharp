#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "value.h"

//Starting with a fixed-size stack
#define STACK_MAX 256

typedef struct{
    Chunk* chunk;
    uint8_t* ip; //Ponits to the Next instruction

    Value stack[STACK_MAX];
    Value* stackTop; //pointer to the empty slot just above the top item
}VM;

typedef enum{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
}InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(Chunk* chunk);

void push(Value value);
Value pop();
Value peek(int distance);

#endif
