#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm; 

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0; //Reset frame count too
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  // Get line number from the current frame
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  size_t instruction = frame->ip - frame->function->chunk.code - 1;
  int line = frame->function->chunk.lines[instruction];
  
  fprintf(stderr, "[line %d] in script\n", line);
  resetStack();
}

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void defineNative(const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

static Value sqrtNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_NUMBER(args[0])) {
    return NIL_VAL; 
  }
  
  double value = AS_NUMBER(args[0]);
  return NUMBER_VAL(sqrt(value));
}

static Value floorNative(int argCount, Value* args) {
  if (argCount != 1 || !IS_NUMBER(args[0])) {
    return NIL_VAL;
  }

  double value = AS_NUMBER(args[0]);
  return NUMBER_VAL(floor(value));
}

static Value inputNative(int argCount, Value* args) {
  //If user passed a prompt string, print it
  if (argCount > 0 && IS_STRING(args[0])) {
    printf("%s", AS_STRING(args[0])->chars);
  }

  //Buffer to hold input (max 1024 chars for now)
  char buffer[1024];
  
  //Read line from stdin
  if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
    size_t length = strlen(buffer);
    
    // Remove the trailing newline '\n' if present
    if (length > 0 && buffer[length - 1] == '\n') {
      buffer[length - 1] = '\0';
      length--;
    }
    
    //Create A-Sharp string and return it
    return OBJ_VAL(copyString(buffer, (int)length));
  }
  
  return NIL_VAL; // Return nil if read fails
}

//Native Function for power operations
//Later Caret (^) can be added as power operator
static Value powNative(int argCount, Value* args) {
  if (argCount != 2) {
    // pow() requires exactly 2 arguments
    return NIL_VAL; 
  }

  if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
    return NIL_VAL;
  }

  double base = AS_NUMBER(args[0]);
  double exponent = AS_NUMBER(args[1]);
  
  return NUMBER_VAL(pow(base, exponent));
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.strings);
  initTable(&vm.globals);

  defineNative("clock", clockNative); //Supporting time
  defineNative("sqrt", sqrtNative); //Supporting Square root
  defineNative("floor", floorNative); // Supporting floor op
  defineNative("input", inputNative); //Taking Input form the user
  defineNative("pow", powNative); //Power Operator;
}

void freeVM() {
  freeTable(&vm.strings);
  freeTable(&vm.globals);
  freeObjects();
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = copyString(chars, length);
  FREE_ARRAY(char, chars, length + 1);

  pop();
  pop();
  push(OBJ_VAL(result));
}

static bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:    return true;
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b);
    default:         return false;
  }
}

static bool call(ObjFunction* function, int argCount) {
  // 1. Check if the arguments passed match the function's expectation
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d.", 
        function->arity, argCount);
    return false;
  }

  // 2. Prevent Stack Overflow
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  // 3. Create a new CallFrame
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->ip = function->chunk.code;
  
  // 4. Point the 'slots' pointer to the start of the arguments on the stack
  frame->slots = vm.stackTop - argCount - 1; 
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        // Run the C function immediately
        Value result = native(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount + 1; //Clean up the stack (pop args + the function itself)
        push(result);//Push the result back onto the stack
        return true;
      }
      case OBJ_FUNCTION: 
        return call(AS_FUNCTION(callee), argCount);
      default:
        break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}

static InterpretResult run() {
  // Grab the current frame info
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

// Macros use 'frame->ip' now
#define READ_BYTE() (*frame->ip++) 
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

#define READ_CONSTANT() \
    (frame->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())

#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    // disassemble using frame's chunk and ip
    disassembleInstruction(&frame->function->chunk, 
        (int)(frame->ip - frame->function->chunk.code));
#endif

    uint8_t instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_NIL:   push(NIL_VAL); break;
      case OP_TRUE:  push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_POP:   pop(); break;

      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]); // <--- FIX: Use frame->slots
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0); // <--- FIX: Use frame->slots
        break;
      }

      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }

      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }

      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if (tableSet(&vm.globals, name, peek(0))) {
          tableDelete(&vm.globals, name); 
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }

      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) {
          frame->ip += offset;
        }
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }

      case OP_CALL: {
        int argCount = READ_BYTE();
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        // Update the frame pointer
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }

      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }

      case OP_RETURN: {
        Value result = pop();       // 1. Pop the return value
        vm.frameCount--;            // 2. Discard the current frame (exit function)
        
        // Edge Case: If we just returned from the Main Script, we are done!
        if (vm.frameCount == 0) {
          pop();                    // Pop the main function object
          return INTERPRET_OK;
        }

        vm.stackTop = frame->slots; // 3. Discard local variables from the stack
        push(result);               // 4. Push the return value back for the caller
        
        // 5. Restore the frame pointer to the caller
        frame = &vm.frames[vm.frameCount - 1];
        break;
      }
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(ObjFunction* function) {
  push(OBJ_VAL(function));
  
  // Set up the first Call Frame
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slots = vm.stack;

  return run();
}