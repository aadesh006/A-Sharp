fun sayHi() { 
  print "Hi"; 
}
print sayHi;

print "";
print "Local Scope & Shadowing";
var value = "Global Value";
{
  var value = "Local Value";
  print "Inside block: " + value;     // Should print "Local Value"
}
print "Outside block: " + value;      // Should print "Global Value"

print "";
print "While Loop";
var i = 3;
while (i > 0) {
  // Print label and number separately to avoid type error
  print "Counting down..."; 
  print i;
  i = i - 1;
}
print "Liftoff!";
print "";
print "For Loop (Fibonacci Sequence)";
// Calculating the first 10 Fibonacci numbers
var a = 0;
var b = 1;
for (var i = 0; i < 5; i = i + 1) {
  print a;
  var temp = a + b;
  a = b;
  b = temp;
}

//fun greet() {
//  print "Hello From A-Sharp";
//}

print "";
print "=== Execution Complete ===";
