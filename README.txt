Hello!

This program (binary matcher) implements pattern matching algorithm at the bit level.

It takes following values as command line arguments:
* Hex encoded bit pattern.
Only pure stream of hex digits (characters 0-9, a-f, A-F) without any leading prefix (0x or 0X) is acceptable. The pattern is what we are going to look for.
* Decimal number of the relevant bits in the pattern.
The program treats binary data in a big-endian way: so the more significant bytes come earlier in the byte stream. In adjacent bytes, the least significant bits of the former byte precede the most significant bits of the latter byte. Given that concept, only the specified number of initial bits comprise the pattern.

So, typical usage of the program is this:
    bitmatch <pattern> <bits nr>

Binary matcher reads data from the standard input and tries to locate bit pattern in there. It exits with 0 if match is found. If data doesn't contain bit pattern (regardless of the byte boundaries), the program exits with 1.
Different error conditions (for instance, incorrect command line arguments) cause different non-zero exit codes. Among such codes are:
 * 3 - Usage Error         - Lack or excess of command line arguments.
 * 4 - Malformed arguments - There are incorrect command line arguments. For instance, number of bits is not a valid representation of decimal integer, or pattern includes incorrect character.
 * 5 - No Memory           - Failed to request memory from the operating system. Unlikely error.
 * 6 - Input/Output error  - The operating system indicated an error during input/output operations. Unlikely error.
In addition to these codes, a message is printed to standard error to facilitate debugging.
Correct operation of the program produces no messages.

To build the program, run the following instruction:
$ gcc -DNDEBUG -O2 -o bitmatch bitmatch.c

That's it!

Looking forward to hearing your feedback.
