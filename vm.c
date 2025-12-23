#include <stdio.h>
#include "common.h"
#include "debug.h"
#include "vm.h"

VM vm;

void initVM(){
    vm.stackTop = vm.stack;
}

void freeVM(){

}

void push(Value value){
    *vm.stackTop = value; //put the value in empty slot
    vm.stackTop++; //Move the pointer up
}//Not checking for overflow yet!

Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

Value pop(){
    vm.stackTop--;
    return *vm.stackTop;
}

//The Engine Loop
static InterpretResult run(){
    //macro: Read the byte currently pointed at by ip, then advance ip
    #define READ_BYTE() (*vm.ip++)
    //macro: Read the next, use it as an index and look up the constant
    #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])

    //macro: BINARY_OP
    #define BINARY_OP(op) \
        do { \
          if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){ \
            /*runtime error*/ \
            return INTERPRET_RUNTIME_ERROR; \
          } \
          double b = AS_NUMBER(pop()); \
          double a = AS_NUMBER(pop()); \
          push (NUMBER_VAL(a op b)); \
        }while(false)

    for(;;){
        #ifndef DEBUG_TRACE_EXECUTION
        printf("        ");
        for(Value* slot = vm.stack; slot < vm.stackTop; slot++){
            printf("[ ");
            printfValue(*slot);
            printf(" ]");
        }
        printf("\n");
        // Disassemble the current instruction so we see what's happening
    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif

    uint8_t instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant); // Push the number onto the stack
        break;
      }

      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;

      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;
      
      case OP_NEGATE:   
        if (!IS_NUMBER(peek(0))){
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;

      case OP_RETURN: {
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
      }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(Chunk* chunk) {
  vm.chunk = chunk;
  vm.ip = vm.chunk->code; // Start IP at the beginning of the chunk
  return run();
}