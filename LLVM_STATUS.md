# LLVM Backend Implementation Status

## ✅ Fully Implemented Features

### Core Operations
- [x] Arithmetic: `+`, `-`, `*`, `/`, negation
- [x] Comparison: `>`, `<`, `>=`, `<=`, `==`, `!=`
- [x] Logical: AND, OR (via control flow)

### Variables
- [x] Local variables (stack slots with `alloca`)
- [x] Global variables (module-level `@globals`)
- [x] Proper lexical scoping

### Control Flow
- [x] `if/else` statements
- [x] `while` loops
- [x] `for` loops
- [x] Short-circuit evaluation
- [x] Two-pass translation with BlockMap for all jump targets

### Functions
- [x] User-defined functions with parameters
- [x] Return statements
- [x] Recursive functions (factorial, fibonacci tested)
- [x] Function pointers stored as doubles on stack
- [x] Early returns in function bodies

### Native Functions
- [x] `sqrt(n)` — square root
- [x] `floor(n)` — floor operation
- [x] `pow(a, b)` — exponentiation (LLVM exclusive)
- [x] `clock()` — CPU time with int64→double conversion

### I/O
- [x] `print` statement via printf

## ❌ Not Implemented

### Closures
- [ ] Upvalue capture (`OP_GET_UPVALUE`, `OP_SET_UPVALUE`)
- [ ] Closure heap allocation (`OP_CLOSE_UPVALUE`)

**Why not implemented:** Closures require heap-allocated closure structs to hold
captured variables. This would need significant infrastructure:
- Define closure struct layout in LLVM
- Allocate closures on heap
- Pass closure pointers alongside function pointers
- Update call convention to handle closure contexts

**Workaround:** Local functions without upvalues work fine. Global functions
and recursion are fully supported.

### Additional Natives
- [ ] `input(prompt)` — requires string I/O
- [ ] String operations — currently only doubles are supported

### String Features
- [ ] String concatenation in `OP_ADD`
- [ ] String comparison
- [ ] String literals beyond print

**Why not implemented:** A# uses `ObjString` heap objects for strings. LLVM
backend would need to:
- Represent A# object types (tagged unions or separate paths)
- Implement string interning in JIT code
- Handle GC interaction with JIT-compiled code

## Test Coverage

**Comprehensive test:** `test_llvm.as` covers all implemented features
- All outputs match VM except boolean display (`1`/`0` vs `true`/`false`)
- Recursive functions tested up to `factorial(5)`, `fib(10)`

## Build System

- `make` → builds `asharp` (bytecode VM, no LLVM dependency)
- `make llvm` → builds `asharp_llvm` (LLVM JIT enabled with `-DUSE_LLVM`)
- Both binaries coexist without conflicts
- Zero compiler warnings on both targets

## Performance Notes

LLVM backend shows faster execution for loops and recursion due to:
- Native code generation vs bytecode interpretation
- LLVM optimizations (constant folding, inlining, etc.)
- Direct register usage vs stack machine

Observable in `clock()` measurements: recursive `fib(10)` and tight loops
run faster in LLVM mode.

## Architecture

See `explain.md` for detailed technical documentation on:
- LLVM concepts and IR structure
- Local variables (alloca + mem2reg)
- Global variables (module globals)
- Control flow (BlockMap two-pass translation)
- User functions (pre-registration, compilation, calling convention)
- Native functions (extern declarations, special handling for `clock()`)
