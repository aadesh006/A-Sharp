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
print "While Loop (Countdown)";
var i = 3;
while (i > 0) {
  print "Counting down... " + i;
  i = i - 1;
}
print "Liftoff!";

print "";
print "For Loop (Fibonacci Sequence)";
// Calculating the first 10 Fibonacci numbers
var a = 0;
var b = 1;

print "0: " + a;
print "1: " + b;

for (var k = 2; k < 10; k = k + 1) {
  var temp = a + b;
  a = b;
  b = temp;
  print k + ": " + temp;
}

print "";
print "=== Execution Complete ===";
