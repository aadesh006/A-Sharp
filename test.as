print "=== Test 1: Basic Functions & Globals ===";
var globalMsg = "Global Variable";

fun printGlobal() {
  print globalMsg; // Works because it's global
}

printGlobal();


print "=== Test 2: First-Class Functions (Passing) ===";
fun runner(fn) {
  print "Running function...";
  fn();
}

fun hello() {
  print "Hello from inside!";
}

// We are passing 'hello' as a value!
runner(hello);


print "=== Test 3: Returning Functions ===";
fun getPrinter() {
  fun inner() {
    print "I was returned!";
  }
  return inner; // We are returning a closure!
}

var myFunc = getPrinter();
myFunc();


print "=== Test 4: Native Functions ===";
print "2^10 (Power):";
print pow(2, 10);

print "Square root of 25:";
print sqrt(25);


print "=== Test 5: Recursion (Stack Stress Test) ===";
fun fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

var start = clock();
print fib(10); // Should be 55
print "Time taken:";
print clock() - start;