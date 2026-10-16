/*
 * llvm_backend.c — A# LLVM JIT Backend
 *
 * HOW THIS WORKS (big picture):
 *
 * 1. We call A#'s existing compiler (compiler.c) on the source code.
 *    This produces an ObjFunction containing a Chunk — a flat array of
 *    bytecode opcodes (OP_CONSTANT, OP_ADD, OP_PRINT, etc.).
 *
 * 2. We create an LLVM "Module" — think of this as a container for all
 *    the IR (Intermediate Representation) we are about to generate.
 *    IR is LLVM's own platform-independent assembly language.
 *
 * 3. We create a single LLVM function called "asharp_main" inside that module.
 *    This function will hold all the translated instructions.
 *
 * 4. We walk the bytecode chunk opcode by opcode. For each opcode we emit
 *    the equivalent LLVM IR instruction:
 *      OP_CONSTANT  → push a floating-point constant onto our value stack
 *      OP_ADD       → pop two values, emit "fadd" IR instruction, push result
 *      OP_PRINT     → pop a value, call C's printf via IR
 *      etc.
 *
 * 5. We pass the completed module to LLVM's MCJIT (Machine Code JIT).
 *    LLVM optimizes the IR and compiles it to native machine code for
 *    your CPU (x86-64 in this case).
 *
 * 6. We call the JIT-compiled "asharp_main" function. The CPU executes it
 *    directly — no interpreter loop, no opcode dispatch overhead.
 *
 * LLVM KEY CONCEPTS:
 *
 *  LLVMModuleRef     — A compilation unit. Like a .o file but in IR form.
 *  LLVMBuilderRef    — A cursor that knows where to insert the next IR instruction.
 *  LLVMValueRef      — A handle to any IR value: constant, variable, result of an op.
 *  LLVMBasicBlockRef — A straight-line sequence of instructions with no branches.
 *                      Every function starts with an "entry" basic block.
 *  LLVMTypeRef       — LLVM's type system: LLVMDoubleType(), LLVMInt32Type(), etc.
 *
 * WHY DOUBLES?
 *  A#'s Value type uses 'double' for all numbers (like Lua and JavaScript).
 *  So all our arithmetic IR uses LLVM's floating-point instructions (fadd, fsub, etc.)
 *  rather than integer instructions (add, sub).
 */

#ifdef USE_LLVM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// LLVM C API headers
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>

// A# headers
#include "llvm_backend.h"
#include "compiler.h"
#include "chunk.h"
#include "value.h"
#include "object.h"
#include "vm.h"

// ---------------------------------------------------------------------------
// VALUE STACK
//
// The original VM uses a runtime stack to pass values between operations.
// During JIT compilation we need a COMPILE-TIME stack of LLVMValueRef handles.
// Each slot holds the IR value that corresponds to what would be on the VM stack.
//
// Example: compiling "1 + 2"
//   OP_CONSTANT 1  → push LLVMConstReal(double, 1.0)   stack: [1.0]
//   OP_CONSTANT 2  → push LLVMConstReal(double, 2.0)   stack: [1.0, 2.0]
//   OP_ADD         → pop 2.0 and 1.0, emit fadd,
//                    push the LLVMValueRef result       stack: [fadd_result]
// ---------------------------------------------------------------------------
#define LLVM_STACK_MAX 256

typedef struct {
    LLVMValueRef slots[LLVM_STACK_MAX];
    int top; // index of the next free slot (0 = empty)
} LLVMValueStack;

static void stackInit(LLVMValueStack* s) { s->top = 0; }

static void stackPush(LLVMValueStack* s, LLVMValueRef val) {
    if (s->top >= LLVM_STACK_MAX) {
        fprintf(stderr, "[LLVM Backend] Stack overflow during compilation.\n");
        exit(1);
    }
    s->slots[s->top++] = val;
}

static LLVMValueRef stackPop(LLVMValueStack* s) {
    if (s->top <= 0) {
        fprintf(stderr, "[LLVM Backend] Stack underflow during compilation.\n");
        exit(1);
    }
    return s->slots[--s->top];
}

// stackPeek will be used in future milestones (variables, closures).
// __attribute__((unused)) suppresses the compiler warning for now.
static __attribute__((unused)) LLVMValueRef stackPeek(LLVMValueStack* s, int distance) {
    return s->slots[s->top - 1 - distance];
}

// ---------------------------------------------------------------------------
// TRANSLATE CHUNK TO LLVM IR
//
// This is the heart of the backend. We iterate over every byte in the chunk
// and emit the corresponding LLVM IR instruction(s).
//
// Parameters:
//   chunk   — the bytecode chunk produced by A#'s compiler
//   builder — LLVM IR builder (our "cursor" for emitting instructions)
//   printf_fn — a reference to printf declared in our LLVM module, used for print
//   fmt_double — an IR global string "%g\n" used as the printf format string
// ---------------------------------------------------------------------------
static bool translateChunk(Chunk* chunk, LLVMBuilderRef builder,
                            LLVMValueRef printf_fn, LLVMValueRef fmt_double,
                            LLVMValueRef fmt_bool_true, LLVMValueRef fmt_bool_false,
                            LLVMValueRef fmt_nil) {

    LLVMValueStack stack;
    stackInit(&stack);

    int i = 0; // instruction pointer — index into chunk->code[]

    while (i < chunk->count) {
        uint8_t opcode = chunk->code[i];

        switch (opcode) {

            // OP_CONSTANT: the next byte is an index into the constants array.
            // We load that constant value and push its LLVM IR representation.
            case OP_CONSTANT: {
                uint8_t constantIdx = chunk->code[i + 1];
                Value val = chunk->constants.values[constantIdx];

                if (IS_NUMBER(val)) {
                    // LLVMConstReal creates a compile-time floating-point constant in IR.
                    // This is not a runtime allocation — it becomes an immediate value
                    // in the generated machine code (like "mov xmm0, 1.0").
                    LLVMValueRef llvmVal = LLVMConstReal(LLVMDoubleType(), AS_NUMBER(val));
                    stackPush(&stack, llvmVal);
                } else if (IS_BOOL(val)) {
                    // Represent booleans as i1 (1-bit integer) in LLVM IR.
                    LLVMValueRef llvmVal = LLVMConstInt(LLVMInt1Type(), AS_BOOL(val) ? 1 : 0, 0);
                    stackPush(&stack, llvmVal);
                } else if (IS_OBJ(val) && IS_STRING(val)) {
                    // For strings, create a global constant char array in IR.
                    const char* str = AS_CSTRING(val);
                    LLVMValueRef llvmStr = LLVMBuildGlobalStringPtr(builder, str, "str");
                    stackPush(&stack, llvmStr);
                } else {
                    fprintf(stderr, "[LLVM Backend] Unsupported constant type.\n");
                    return false;
                }
                i += 2; // skip opcode + index byte
                break;
            }

            // OP_NIL, OP_TRUE, OP_FALSE: push literal values.
            case OP_NIL: {
                // Represent nil as a string "nil" printed literally.
                LLVMValueRef nilStr = LLVMBuildGlobalStringPtr(builder, "nil", "nil_str");
                stackPush(&stack, nilStr);
                i++;
                break;
            }

            case OP_TRUE: {
                LLVMValueRef llvmVal = LLVMConstInt(LLVMInt1Type(), 1, 0);
                stackPush(&stack, llvmVal);
                i++;
                break;
            }

            case OP_FALSE: {
                LLVMValueRef llvmVal = LLVMConstInt(LLVMInt1Type(), 0, 0);
                stackPush(&stack, llvmVal);
                i++;
                break;
            }

            // OP_POP: discard the top stack value (result unused by an expression statement).
            case OP_POP: {
                if (stack.top > 0) stackPop(&stack);
                i++;
                break;
            }

            // OP_NEGATE: pop one value, emit fneg (floating-point negate), push result.
            // IR: %result = fneg double %operand
            case OP_NEGATE: {
                LLVMValueRef operand = stackPop(&stack);
                LLVMValueRef result = LLVMBuildFNeg(builder, operand, "neg");
                stackPush(&stack, result);
                i++;
                break;
            }

            // OP_NOT: logical not. In A#, 'not' follows Lox rules:
            //   false and nil are falsy; everything else is truthy.
            // For now we handle the numeric case: 0.0 → true, else → false.
            case OP_NOT: {
                LLVMValueRef operand = stackPop(&stack);
                // Compare operand == 0.0 (OEQ = ordered equal, NaN-safe)
                LLVMValueRef zero = LLVMConstReal(LLVMDoubleType(), 0.0);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOEQ, operand, zero, "not");
                // Extend i1 → double so the stack stays homogeneous for arithmetic.
                LLVMValueRef result = LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "not_d");
                stackPush(&stack, result);
                i++;
                break;
            }

            // Arithmetic opcodes: pop two operands, emit the IR instruction, push result.
            //
            // LLVM floating-point instructions:
            //   fadd — floating-point add
            //   fsub — floating-point subtract
            //   fmul — floating-point multiply
            //   fdiv — floating-point divide
            //
            // Note: operands are in stack order — right operand on top (popped first).
            case OP_ADD: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef result = LLVMBuildFAdd(builder, a, b, "add");
                stackPush(&stack, result);
                i++;
                break;
            }

            case OP_SUBTRACT: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef result = LLVMBuildFSub(builder, a, b, "sub");
                stackPush(&stack, result);
                i++;
                break;
            }

            case OP_MULTIPLY: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef result = LLVMBuildFMul(builder, a, b, "mul");
                stackPush(&stack, result);
                i++;
                break;
            }

            case OP_DIVIDE: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef result = LLVMBuildFDiv(builder, a, b, "div");
                stackPush(&stack, result);
                i++;
                break;
            }

            // Comparison opcodes: produce a boolean (i1) result.
            // We use LLVM's fcmp instruction with ordered predicates.
            //   LLVMRealOGT — ordered greater than
            //   LLVMRealOLT — ordered less than
            //   LLVMRealOEQ — ordered equal
            //
            // OP_EQUAL also handles != via OP_NOT following it.
            case OP_GREATER: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOGT, a, b, "gt");
                // Convert i1 → double for uniform stack type
                LLVMValueRef result = LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "gt_d");
                stackPush(&stack, result);
                i++;
                break;
            }

            case OP_LESS: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOLT, a, b, "lt");
                LLVMValueRef result = LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "lt_d");
                stackPush(&stack, result);
                i++;
                break;
            }

            case OP_EQUAL: {
                LLVMValueRef b = stackPop(&stack);
                LLVMValueRef a = stackPop(&stack);
                LLVMValueRef cmp = LLVMBuildFCmp(builder, LLVMRealOEQ, a, b, "eq");
                LLVMValueRef result = LLVMBuildUIToFP(builder, cmp, LLVMDoubleType(), "eq_d");
                stackPush(&stack, result);
                i++;
                break;
            }

            // OP_PRINT: pop the top value and print it using printf.
            //
            // In LLVM IR, calling a C function looks like:
            //   %result = call i32 @printf(i8* %fmt, double %val)
            //
            // LLVMBuildCall2 emits this call instruction. We pass:
            //   - the function type of printf
            //   - the printf function reference (declared in our module)
            //   - the format string and value as arguments
            case OP_PRINT: {
                LLVMValueRef val = stackPop(&stack);

                // Build printf call: printf("%g\n", val)
                LLVMTypeRef printfArgTypes[] = { LLVMPointerType(LLVMInt8Type(), 0) };
                LLVMTypeRef printfType = LLVMFunctionType(
                    LLVMInt32Type(), printfArgTypes, 1, /*isVarArg=*/1);

                LLVMValueRef printfArgs[] = { fmt_double, val };
                LLVMBuildCall2(builder, printfType, printf_fn, printfArgs, 2, "");
                i++;
                break;
            }

            // OP_RETURN at the top level means end of script.
            // We pop the implicit return value (nil) and emit a void return.
            case OP_RETURN: {
                // Pop return value if present (top-level script returns nil)
                if (stack.top > 0) stackPop(&stack);
                LLVMBuildRetVoid(builder);
                i++;
                goto done; // exit the translation loop
            }

            default: {
                // For opcodes we haven't implemented yet, skip over them.
                // We print a warning so you know what's missing.
                fprintf(stderr, "[LLVM Backend] Warning: unhandled opcode %d at offset %d, skipping.\n",
                        opcode, i);
                i++;
                break;
            }
        }
    }

done:
    // If we fell off the end without an OP_RETURN, emit one now.
    // This is needed for chunks that don't end cleanly (shouldn't happen
    // in a correct A# program, but defensive coding is good practice).
    {
        LLVMBasicBlockRef lastBlock = LLVMGetInsertBlock(builder);
        if (lastBlock && !LLVMGetBasicBlockTerminator(lastBlock)) {
            LLVMBuildRetVoid(builder);
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// runWithLLVM — PUBLIC ENTRY POINT
//
// Steps:
//  1. Use A#'s existing compiler to compile source → ObjFunction (with Chunk)
//  2. Set up LLVM module + function + basic block
//  3. Declare external C functions we'll call (printf)
//  4. Translate the chunk to IR via translateChunk()
//  5. Verify the IR is well-formed (LLVM will catch our mistakes)
//  6. JIT compile + execute
//  7. Clean up
// ---------------------------------------------------------------------------
LLVMBackendResult runWithLLVM(const char* source) {

    // ---- STEP 1: Compile A# source to bytecode using the existing compiler ----
    //
    // We initialize the VM just for the compiler's use (it needs the string
    // interning table and object allocator). We won't use vm.c's interpreter.
    initVM();

    ObjFunction* function = compile(source);
    if (function == NULL) {
        freeVM();
        return LLVM_COMPILE_ERROR;
    }

    Chunk* chunk = &function->chunk;

    // ---- STEP 2: Initialize LLVM targets ----
    //
    // LLVM supports many CPU architectures. "NativeTarget" means:
    // compile for the CPU this program is running on right now (e.g. x86-64).
    // These three calls initialize the target-specific code generators.
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    LLVMLinkInMCJIT(); // Link in the Machine Code JIT engine

    // ---- STEP 3: Create the LLVM Module ----
    //
    // A Module is the top-level container for IR. Think of it as a single
    // compilation unit (like one .c file). Everything we generate goes in here.
    LLVMModuleRef module = LLVMModuleCreateWithName("asharp_module");

    // ---- STEP 4: Declare printf in our module ----
    //
    // printf is a C standard library function. We can't define it ourselves —
    // we just tell LLVM "there's a function called printf with this signature,
    // trust me it exists." The JIT linker will find it at runtime.
    //
    // printf signature: int printf(const char* fmt, ...)
    // In LLVM IR: declare i32 @printf(i8*, ...)
    LLVMTypeRef printfParamTypes[] = { LLVMPointerType(LLVMInt8Type(), 0) };
    LLVMTypeRef printfType = LLVMFunctionType(
        LLVMInt32Type(),    // return type: int
        printfParamTypes,   // first param: char*
        1,                  // one named param
        1                   // isVarArg = true (the "..." in printf)
    );
    LLVMValueRef printf_fn = LLVMAddFunction(module, "printf", printfType);

    // ---- STEP 5: Create our main function in the module ----
    //
    // This is the function that will hold all the translated A# code.
    // It takes no arguments and returns void.
    //
    // In IR this looks like:
    //   define void @asharp_main() { ... }
    LLVMTypeRef mainFnType = LLVMFunctionType(LLVMVoidType(), NULL, 0, 0);
    LLVMValueRef mainFn = LLVMAddFunction(module, "asharp_main", mainFnType);

    // ---- STEP 6: Create the entry basic block ----
    //
    // A basic block is a straight-line sequence of instructions.
    // Every function needs at least one. The "entry" block is where
    // execution starts when the function is called.
    LLVMBasicBlockRef entryBlock = LLVMAppendBasicBlock(mainFn, "entry");

    // ---- STEP 7: Create the IR builder ----
    //
    // The builder is our "pen" for writing IR. We position it at the
    // end of the entry block, meaning new instructions are appended there.
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entryBlock);

    // ---- STEP 8: Create format strings for printf ----
    //
    // LLVMBuildGlobalStringPtr creates a global constant string in the module.
    // In IR: @fmt_double = global [4 x i8] c"%g\0A\00"
    // The builder call returns an i8* pointer to it for use as printf's first arg.
    LLVMValueRef fmt_double       = LLVMBuildGlobalStringPtr(builder, "%g\n",    "fmt_double");
    LLVMValueRef fmt_bool_true    = LLVMBuildGlobalStringPtr(builder, "true\n",  "fmt_true");
    LLVMValueRef fmt_bool_false   = LLVMBuildGlobalStringPtr(builder, "false\n", "fmt_false");
    LLVMValueRef fmt_nil          = LLVMBuildGlobalStringPtr(builder, "nil\n",   "fmt_nil");

    // ---- STEP 9: Translate the bytecode chunk to LLVM IR ----
    bool ok = translateChunk(chunk, builder, printf_fn,
                             fmt_double, fmt_bool_true, fmt_bool_false, fmt_nil);

    if (!ok) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(module);
        freeVM();
        return LLVM_COMPILE_ERROR;
    }

    // ---- STEP 10: Verify the IR ----
    //
    // LLVMVerifyModule checks that the IR we generated is valid.
    // For example: every basic block must end with a terminator instruction
    // (ret, br, etc.), every value must be defined before use, etc.
    // If we made a mistake in translateChunk(), this catches it with a
    // helpful error message instead of a cryptic crash.
    char* error = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "[LLVM Backend] IR verification failed:\n%s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(module);
        freeVM();
        return LLVM_COMPILE_ERROR;
    }
    LLVMDisposeMessage(error);

    // Optionally: dump the generated IR to stderr for inspection.
    // Uncomment this during debugging to see what IR was produced:
    // LLVMDumpModule(module);

    // ---- STEP 11: Create the JIT execution engine ----
    //
    // The execution engine takes our module and JIT-compiles it to
    // native machine code for the current CPU.
    //
    // MCJIT (Machine Code JIT) is LLVM's JIT backend. It compiles entire
    // functions at once (as opposed to tracing JITs that compile hot paths).
    LLVMExecutionEngineRef engine;
    error = NULL;
    if (LLVMCreateExecutionEngineForModule(&engine, module, &error)) {
        fprintf(stderr, "[LLVM Backend] Failed to create JIT engine:\n%s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeBuilder(builder);
        freeVM();
        return LLVM_RUNTIME_ERROR;
    }

    // ---- STEP 12: Look up and call our compiled function ----
    //
    // LLVMGetFunctionAddress finds the native machine code address of
    // "asharp_main" that the JIT just compiled.
    // We cast it to a function pointer and call it like a regular C function.
    typedef void (*ASharpMainFn)(void);
    ASharpMainFn asharpMain = (ASharpMainFn)(uintptr_t)
        LLVMGetFunctionAddress(engine, "asharp_main");

    if (asharpMain == NULL) {
        fprintf(stderr, "[LLVM Backend] Could not find JIT-compiled function.\n");
        LLVMDisposeExecutionEngine(engine);
        LLVMDisposeBuilder(builder);
        freeVM();
        return LLVM_RUNTIME_ERROR;
    }

    // Execute the JIT-compiled A# program!
    asharpMain();

    // ---- STEP 13: Clean up ----
    // LLVMDisposeExecutionEngine also frees the module (it takes ownership).
    LLVMDisposeExecutionEngine(engine);
    LLVMDisposeBuilder(builder);
    freeVM();

    return LLVM_OK;
}

#endif // USE_LLVM
