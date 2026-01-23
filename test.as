//This File include some example codes for the `A-Sharp` language
// ==========================================
//  The AS Language Showcase
//  Current Version: 0.1
// ==========================================

print "Variables and Strings";
var language = "A-Sharp";
var version = "0.1";
var greeting = "Hello from " + language + " v" + version + "!";
print greeting;

print ""; 
print "Logic and Branching";
var isComplete = false;
var chapter = 23;

if (chapter >= 23) {
  print "Status: Turing Complete (Loops & Ifs supported)";
} else {
  print "Status: Still learning...";
}

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

fun sayHi() { print "Hi"; }
print sayHi; // Should print <fn sayHi>


print "";
print "=== Execution Complete ===";
