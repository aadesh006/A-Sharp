var a = "garbage";
var b = " collection";
var i = 0;

while (i < 100000) {
  var dump = a + b; 
  i = i + 1;
}

print "Done! If I didn't crash, the GC saved me!";