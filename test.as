fun add(a, b, c) {
  print "Adding three numbers...";
  return a + b + c;
}

var sum = add(10, 20, 30);
print sum;

fun isEven(n) {
  if (n < 0) {
    print "Negative number!";
    return false;
  }
  
  // A naive way to check evenness using a loop
  while (n > 0) {
    n = n - 2;
  }
  
  if (n == 0) return true;
  return false;
}

print isEven(10);
print isEven(7);

print "============Recurvise Function==========="

fun fib(n) {
  if (n < 2) return n;
  return fib(n - 2) + fib(n - 1);
}

var start = 0;
while (start < 10) {
  print fib(start);
  start = start + 1;
}

print "==========Execution Complete==========";