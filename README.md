# A-Sharp Programming Language

A bytecode virtual machine and interpreter for the A-Sharp programming language, written in C. This implementation follows the architecture described in Part III of **Crafting Interpreters** by Robert Nystrom.

**Project Status:** Active development — core features including control flow, scoping, closures, functions, native functions, user input, and garbage collection are fully functional.

---

## Overview

A-Sharp is a dynamically-typed scripting language with a stack-based bytecode VM. It features:

- **Clean syntax** for variables, expressions, and control flow
- **Lexical scoping** with local and global variables
- **First-class functions and closures** with upvalue capture
- **User-defined functions** with return statements
- **Native functions** for common operations
- **User input** capabilities for interactive programs
- **Interactive REPL** with line editing and command history
- **Tri-color mark-and-sweep garbage collector** for automatic memory management
- **Efficient bytecode execution** via a custom virtual machine

---

## Features

### Implemented

- **Lexical Analysis**: Tokenizes source code into a stream of tokens
- **Compilation**: Pratt parser with single-pass bytecode generation
- **Virtual Machine**: Stack-based bytecode interpreter with optimized dispatch
- **Data Types**: Numbers, strings, booleans, and nil
- **Variables**: Local and global variables with lexical scoping
- **Operators**: Arithmetic (`+`, `-`, `*`, `/`), comparison (`<`, `>`, `<=`, `>=`), equality (`==`, `!=`), logical (`and`, `or`, `not`)
- **Control Flow**:
  - `if`/`else` statements for conditional execution
  - `while` loops for iteration
  - `for` loops with C-style syntax
- **Functions**: User-defined functions with parameters and `return` statements
- **Closures**: First-class functions with full upvalue capture and promotion to the heap when variables go out of scope
- **Garbage Collection**: Tri-color mark-and-sweep GC with dynamic heap growth thresholds
- **String Interning**: All strings are deduplicated in a global hash table for efficient equality checks and memory usage
- **Native Functions**: Built-in functions including:
  - `clock()` — Returns current time in seconds since epoch
  - `sqrt(n)` — Calculates square root of a number
  - `floor(n)` — Returns the largest integer less than or equal to a number
- **I/O**:
  - `print` statement for output
  - `input(prompt)` function for reading user input
- **REPL**: Interactive shell with readline support

### Upcoming

- Classes and objects
- Inheritance
- Standard library expansion
- Module system

---

## Quick Start

### Prerequisites

- C compiler (GCC or Clang)
- GNU Make
- Readline library (for REPL features)

**Installation on Ubuntu/Debian/WSL:**
```bash
sudo apt update
sudo apt install build-essential libreadline-dev
```

**Installation on macOS:**
```bash
brew install readline
# Note: You may need to set CPPFLAGS and LDFLAGS for the compiler
export CPPFLAGS="-I/opt/homebrew/opt/readline/include"
export LDFLAGS="-L/opt/homebrew/opt/readline/lib"
```

### Build

```bash
# Clone the repository
git clone https://github.com/aadesh006/A-Sharp.git
cd A-Sharp

# Build the interpreter
make

# Clean build (if needed)
make clean && make
```

The compiled executable `asharp` will be created in the project root.

---

## Usage

### Interactive REPL

Start the interactive shell:
```bash
./asharp
```

Try some expressions:
```javascript
> print 1 + 2 * 3;
7
> var name = "A-Sharp";
> print "Hello from " + name;
Hello from A-Sharp
> print clock();
1738612345.678
```

### Running Scripts

Execute a source file (`.as` extension):
```bash
./asharp script.as
```

### Example Programs

**Basic Arithmetic:**
```javascript
print 1 + 2 * (3 - 4);  // -1
```

**Variables and Scoping:**
```javascript
var a = "Programming with ";
var b = "A-Sharp";

{
  // Local scope
  var a = 15;
  var b = 15;
  print a + b;  // 30
}

print a + b;  // Programming with A-Sharp
```

**Functions:**
```javascript
fun greet() {
  print "Hello From Greet Function!";
}

greet();

fun add(a, b) {
  return a + b;
}

var result = add(10, 20);
print result;  // 30

fun fib(n) {
  if (n < 2) return n;
  return fib(n - 2) + fib(n - 1);
}

print fib(10);  // 55
```

**Closures:**
```javascript
fun makeCounter() {
  var count = 0;
  fun increment() {
    count = count + 1;
    return count;
  }
  return increment;
}

var counter = makeCounter();
print counter();  // 1
print counter();  // 2
print counter();  // 3
```

**Native Functions:**
```javascript
// Get current time
var startTime = clock();
print "Start time: " + startTime;

// Mathematical operations
print "Square root of 64 is:";
print sqrt(64);   // 8

print "Floor of 5.95 is:";
print floor(5.95); // 5
```

**User Input:**
```javascript
print "Calculate Hypotenuse";

var a = input("Enter side A: ");
var b = input("Enter side B: ");

// Note: input() returns strings; numeric coercion happens automatically
// in arithmetic expressions
var sumOfSquares = (a * a) + (b * b);
var hypotenuse = sqrt(sumOfSquares);

print "The hypotenuse is: " + hypotenuse;
```

**Conditional Statements:**
```javascript
var age = 20;

if (age >= 18) {
  print "Adult";
} else {
  print "Minor";
}
```

**While Loops:**
```javascript
var i = 0;
while (i < 5) {
  print i;
  i = i + 1;
}
```

**For Loops:**
```javascript
for (var i = 0; i < 10; i = i + 1) {
  print i * i;
}
```

**Fibonacci Generator:**
```javascript
var a = 0;
var b = 1;

for (var i = 0; i < 10; i = i + 1) {
  print a;
  var temp = a + b;
  a = b;
  b = temp;
}
```

More examples can be found in `test.as`.

---

## Built-in Functions Reference

### `clock()`
Returns the current time in seconds since the Unix epoch (January 1, 1970).

```javascript
var start = clock();
// ... some code ...
var end = clock();
print "Elapsed time: " + (end - start) + " seconds";
```

### `sqrt(n)`
Returns the square root of a number. `n` must be non-negative.

```javascript
print sqrt(16);    // 4
print sqrt(2);     // 1.414...
print sqrt(100);   // 10
```

### `floor(n)`
Returns the largest integer less than or equal to `n`.

```javascript
print floor(5.95);   // 5
print floor(5.05);   // 5
print floor(-2.3);   // -3
```

### `input(prompt)`
Displays `prompt` and reads a line of text from the user. Returns a string.

```javascript
var name = input("Enter your name: ");
print "Hello, " + name;
```

**Note:** `input()` returns strings. Values are automatically coerced to numbers when used in arithmetic expressions.

---

## Project Structure

```
A-Sharp/
├── main.c              # Entry point, REPL, and CLI interface
├── scanner.{c,h}       # Lexical analyzer (tokenizer)
├── compiler.{c,h}      # Pratt parser and single-pass bytecode compiler
├── vm.{c,h}            # Stack-based virtual machine and bytecode interpreter
├── chunk.{c,h}         # Bytecode chunk data structure and opcode definitions
├── value.{c,h}         # Runtime value representation
├── object.{c,h}        # Heap-allocated objects (strings, functions, closures, upvalues)
├── memory.{c,h}        # Memory management and mark-and-sweep garbage collector
├── table.{c,h}         # Open-addressing hash table (globals, string interning)
├── debug.{c,h}         # Bytecode disassembler and debugging utilities
├── Makefile            # Build configuration
└── test.as             # Sample programs
```

### Architecture Overview

1. **Scanner** (`scanner.c/h`) — Converts source text into a flat stream of tokens
2. **Compiler** (`compiler.c/h`) — Parses tokens using Pratt parsing and emits bytecode directly (no AST)
3. **Chunk** (`chunk.c/h`) — Stores bytecode instructions, a constant pool, and per-instruction line numbers
4. **VM** (`vm.c/h`) — Executes bytecode on a stack-based virtual machine with up to 64 call frames
5. **Object** (`object.c/h`) — Manages heap-allocated runtime objects: strings, functions, closures, and upvalues
6. **Memory** (`memory.c/h`) — Handles dynamic allocation and the tri-color mark-and-sweep GC

---

## Development

### Architecture Principles

- **Single-pass compiler** — Bytecode is emitted directly during parsing without building an AST, keeping compilation fast.
- **Stack-based VM** — All operations push/pop values from a contiguous value stack. Call frames hold a pointer into this stack as their local window.
- **Closures via upvalues** — Captured variables live on the stack while their enclosing function is active, then are "closed over" and moved to the heap when that scope exits (`OP_CLOSE_UPVALUE`).
- **Tri-color mark-and-sweep GC** — The garbage collector maintains a gray worklist. `bytesAllocated` is tracked against a `nextGC` threshold to trigger collection automatically.
- **String interning** — All strings are stored in a global hash table (`vm.strings`); equality can therefore be checked with pointer comparison.
- **Hash tables** (`table.c/h`) — Used for both global variable lookup and string interning, with open addressing and a load factor of 75%.

### Bytecode Opcodes

| Opcode | Description |
|---|---|
| `OP_CONSTANT` | Push a constant from the constant pool |
| `OP_NIL` / `OP_TRUE` / `OP_FALSE` | Push literal values |
| `OP_POP` | Discard the top of the stack |
| `OP_GET_LOCAL` / `OP_SET_LOCAL` | Read/write a local variable by stack slot |
| `OP_GET_GLOBAL` / `OP_SET_GLOBAL` / `OP_DEFINE_GLOBAL` | Global variable access via hash table |
| `OP_GET_UPVALUE` / `OP_SET_UPVALUE` / `OP_CLOSE_UPVALUE` | Closure upvalue operations |
| `OP_CLOSURE` | Wrap a function in a closure object and capture upvalues |
| `OP_JUMP` / `OP_JUMP_IF_FALSE` / `OP_LOOP` | Control flow with 16-bit offsets |
| `OP_CALL` | Invoke a function or native |
| `OP_RETURN` | Return from a function, closing any open upvalues |
| `OP_ADD` / `OP_SUBTRACT` / `OP_MULTIPLY` / `OP_DIVIDE` | Arithmetic |
| `OP_EQUAL` / `OP_GREATER` / `OP_LESS` | Comparison |
| `OP_NOT` / `OP_NEGATE` | Unary operations |
| `OP_PRINT` | Print top-of-stack value |

### Adding New Features

1. **Add tokens** in `scanner.h` / `scanner.c` if new keywords or symbols are needed
2. **Extend the parser** in `compiler.c` with Pratt parse rules
3. **Emit bytecode** from the relevant parse function
4. **Add an opcode** to `chunk.h` and handle it in `vm.c`

**Example — adding a modulo operator:**
```c
// 1. scanner.h
TOKEN_MODULO,

// 2. scanner.c
case '%': addToken(TOKEN_MODULO); break;

// 3. compiler.c parse table
[TOKEN_MODULO] = {NULL, binary, PREC_FACTOR},

// 4. chunk.h
OP_MODULO,

// 5. vm.c
case OP_MODULO: BINARY_OP(NUMBER_VAL, %); break;
```

**Adding a new native function:**
```c
// 1. Define in vm.c
static Value absNative(int argCount, Value* args) {
  double n = AS_NUMBER(args[0]);
  return NUMBER_VAL(n < 0 ? -n : n);
}

// 2. Register in initVM()
defineNative("abs", absNative);

// 3. Use in A-Sharp
print abs(-42);  // 42
```

### Debugging

Enable execution tracing by uncommenting in `common.h`:
```c
#define DEBUG_TRACE_EXECUTION   // prints stack state + instruction before each op
#define DEBUG_PRINT_CODE        // disassembles every compiled chunk
```

Or use the disassembler directly:
```c
#include "debug.h"
disassembleChunk(&chunk, "my chunk");
```

Build with debug symbols:
```bash
make CFLAGS="-g -O0"
```

### Testing

```bash
./asharp test.as
```

---

## Contributing

Contributions are welcome! Areas for improvement:

- Additional native functions (string manipulation, file I/O, type conversion)
- Object-oriented features (classes, inheritance)
- Standard library expansion
- Improved error messages and diagnostics
- Performance optimizations

Please open an issue to discuss major changes before submitting a pull request.

---

## Author

**Aadesh Chaudhari**  
GitHub: [@aadesh006](https://github.com/aadesh006)
