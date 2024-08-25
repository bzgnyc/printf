# printf(1)

An implementation of the `printf(1)` utility as specified by POSIX.1-2017:
https://pubs.opengroup.org/onlinepubs/9699919799/utilities/printf.html


## Contents

* [Motivation](#Motivation)
* [Design](#Design)
* [Standards](#Standards)
* [Learnings](#Learnings) 


## Motivation

While working on unrelated projects, I was surprised at the challenges and variations in support for working with multibyte strings in general and accessing Unicode characters (e.g. easily generating UTF-8 sequences from Unicode codepoints) across common tools and utilities despite support in the standards going back 20 and 30 years.

Then after looking at the source code to several common `printf(1)` implementations, I was surprised that they all recreated `printf(3)`.  Prior to that my superficial understanding was that `printf(1)` was more or less a wrapper around `printf(3)`, and I was curious why all these implementations bypassed one of the most fundemental functions in the C library.

Additionally I was curious what programs need to do to support non-English languages in general and access the Unicode character set in particular (and create UTF-8 strings specifically) in a standard and portable way.

As such I wrote a `printf(1)` from scratch as a way to explore these issues in the context of a simple, but non-trivial and well-defined utility.  Not "Hello World" and not a word processor (i.e. no "boiling the ocean").


## Design

### Goals
* Full support for wide/multibyte characters in general and Unicode in particular
* Compile and run properly in any environment supporting ANSI C (C89 or latter)
* Use C standard libraries in general and avoid recreating `printf(3)` in particular
* Output whole strings and avoid explicit character-by-character manipulation as much as possible
* Minimize passes over the data -- ideally only touch format operand once and pass string arguments directly to `printf(3)` untouched

### Challenges
* Best case, at least one pass over the format operand is required before it can be given as an argument to `printf(3)`
  - `printf(3)` doesn't actually handle escape sequences
    + It is the C compiler that converts escape sequences in string literals to the platform's native encoding (e.g. `\n` is converted to binary `0x10` by the compiler before it is stored in the resulting executable on platforms where ASCII or a derivative is the native character set)
    + As `printf(3)` expects such sequences to already be translated for output, it does not search its format argument for them
  - Even if `printf(3)` handled escape sequences, a pass is required to identify all conversion specifications in the format operand so that each argument to `printf(1)` can be converted to the proper type before being passed to `printf(3)`
    + All arguments to `printf(3)` following the format string must already be in the type expected by the associated conversion specifier in the format string but a standard command-line C program receives all arguments as character strings
    + Trying to autotype those character strings would be unreliable while adding overhead
* The format operand passed to `printf(1)` must be broken into batches before it can be turned over to `printf(3)`
  - The C standard, at least through C23, requires that all calls to functions, even ones taking a variable number of arguments via Stdarg variadic functions/macros, are made with the number of arguments known at compile time
  - Stdarg allows receiving functions to work with a variable number of arguments of multiple types but it does not allow for the construction of a new argument list from scratch nor adding arguments to an existing list
  - Practically speaking this requires breaking the format operand into batches containing a fixed number of conversion specifications
* While C has included multibyte and wide character string functions and types in the standard since C89, key details are deliberately marked implementation-defined
* Processing the format operand using functions from the standard C library makes it difficult to identify both escape sequences and conversion specifications without multiple passes over the data, but parsing the format operand character-by-character tends towards recreating `printf(3)`
  
### Decisions
* Work primarily in (narrow) character strings and output using standard `printf(3)` function
  - Use `wprintf(3)` when conversion specifiers reference wide character strings (see [Standards](#Standards)), converting potential multibyte strings to wide character strings in the current/default locale using standard C89 wide/multibyte string functions
  - Do not directly generate UTF-8 sequences nor round-trip every string through Unicode (e.g. don't convert every string back and forth between UTF-32)
  - Conditionally define three (so far) different Fromunicode functions to convert Unicode codepoints to the current locale's corresponding multibyte character sequences
    + C23's `c32rtomb(3)` (when available)
    + C89's `wctomb(3)` (only if `__STDC_ISO_10646__` is defined)
    + POSIX's `iconv(3)`
* Format operand parsed into batches of [_Prologue_][_%_][_Format_][_Specifier_][_Epilogue_] with no more than one conversion specification ([_%_][_Format_][_Specifier_] where [_Format_] encompasses the [_flags_][_width_][_.precision_] options of a conversion specification and the length modifiers required for `printf(3)` are invalid for `printf(1)`) per batch
  - As discussed in Challenges, the number of conversion specifications passed to `printf(3)` must be fixed at compile-time and one is a good per-batch natural limit for user-supplied conversion specifications
  - I originally liked the idea that a single invocation of `printf(1)` such as `printf "Some text %s; plus more text.\n" "I need printed"` would be translated into a single `printf("Some text %s; plus more text.\n","I need printed")`
  - While I shifted the logic so the above would be translated to `printf("%s%s%s","Some text ","%s","; plus more text.\n")`, I kept the parsing logic
    + Since the format operand has already been parsed by the time the call is made to `printf(3)`, including already parsed text in the format argument to `printf(3)` just means more text for `printf(3)` to parse with no benefit
    + Though the performance difference is likely immaterial in practice, I measured a 7x increase in performance for extremely large strings using `printf("%s","Lots of text")` versus `printf("Lots of text")`
* Use `sscanf(3)` and an intricate logic tree that incorporates the partial results of each `sscanf(3)` attempt and parses incrementally to identify the end of each batch and break the batch into the above components
  - Character-by-character approach likely would have required a finite state machine and/or simply recreated `printf(3)`
  - Since `sscanf(3)` does not offer complete regular expression support, there is no single format that can be used with it to identify all valid variations of %[Format][Specifier] across an unknown input string (e.g. recognize both  `"%d"` and `"% .5d"` as valid conversion specifications but not include the `"."` from `"%d."`)
  - However, to minimize processing passes over the format operand, it is parsed incrementally and partial results of each `sscanf(3)` matching attempt are used to identify each batch and break it into the above components
* Process the [_Prologue_] and [_Epilogue_] components for escape sequences as a 2nd pass
  - While this has slightly more overhead, it was deemed acceptable relative to the alternative complexitiy
  - Additionally it was deemed acceptable to only have one function for processing escape sequences even though standards specify slightly different escape sequences for format operand and arguments processed with "b" conversion specifier (i.e. `"%b"` format (see [Standards](#Standards))
  - Character-by-character processing deemed acceptable for initial version to handle the combination of two character (e.g. `\n` and octal escape sequences (e.g. `"\123"`) required by the standard in addition to the Unicode escape sequences included as an extension
* Don't scan the [_Format_] and/or [_Specifier_] components for escape sequences since escape sequences are not valid there by standard
* Handle the appearence of `*` in the [_Format_] to indicate using the value of the next argument for the width and/or precision of a conversion specification with a simple substitution prior to passing the format to `printf(3)` for the same reasons that each batch was limited to one conversion specification
* Use "positional" conversion specifications for calls to `printf(3)` so that it doesn't match the wrong argument to a conversion specification when another conversion specification (e.g. the format operand supplied by the user) is invalid
* The "positional" or "number argument" conversion specification would not be supported (see [Standards](#Standards)) in the initial version

### Results
* Output as per standards with key extensions for Unicode escape sequences and wide character output (see [Standards](#Standards))
  - Not yet tested on platforms that supports complex, stateful multibyte encodings like ISO-2022-JP.
  - Not yet tested on platforms supporting multibyte encodings where the byte values for `%`, `\`, and/or `*` may appear in a valid multibyte sequence
* Should compile on any ANSI C system though only tested so far on UNIX systems
  - Support for Unicode escape sequences couldn't be achieved without making use of C23 functions or platform-specific extensions
  - Pre-C23 platforms may require additional compile-time defines (e.g. `HAVE_ICONV`) and/or new platform-specific versions of the fromunicode() function
  - Some `#ifdefs` were required to workaround MacOS X quirks (which may also be required on other platforms not yet tested)
* Most parsing is done with `sscanf(3)` and final output of sanitized formats is handled by `printf(3)`
* The format operand is typically scanned several times while pure string arguments are passed untouched to `printf`
  - Depending on the structure of the format operand, portions may be processed multiple times by multiple `sscanf(3)`
  - Conversion specifications are typically processed multiple times in order to both sanitize it for unexpected/invalid formats (especially those valid for `printf(3)` but not valid for `printf(1)` and then in preparation of the final format argument to `printf(3)`
  - Arguments matched to "s" and "c" conversion specifiers are passed directly as arguments to `printf(3)` with zero additional processing
  - All other conversion specifiers imply some intermediate processing such as conversion to an actual integer or float-point type, translation of escape sequences, and/or conversion to a wide character string
    + Arguments processed by the "Q" conversion specifier are processed twice -- first to translate any escape sequences after which those results are translated into a wide character string (these two passes could potentially made into one unified pass).
    + All other arguments are processed just once before being passed as an argument to `printf(3)` of the appropriate type

### Abandoned Ideas
* Loop through format operand piping `vsscanf(3)` into `vprintf(3)`
* Use `sscanf(3)` for all parsing
* Recursive function for parsing the format string
* Limit each batch to just text or just a format specifiation rather than the [_Prologue_][_%_][_Format_][_Specifier_][_Epilogue_] format above

### Other ideas
* Rewrite routines that process escape sequences, handle width/precision substitutions in conversion specifications, and sanitize conversion specifications to use `sscanf(3)` rather than character-by-character processing
* Convert format operand to a wide character string early and then mostly use wide character strings functions for processing rather than vice-versa
  - This may be required to properly support complex multibyte encodings
  - This may be preferred under Windows
  - This may have higher overhead on UNIX and similar platforms though I don't think it would be material
* Parse format operand into a reusable array structure so that parsing the format operand only happens once per invocation rather than multiple times when there are more arguments than conversion specifications (e.g. `printf "This number %d\n" 1 2 3` requires 3 cycles through the format operand to output according to POSIX specifications)
* Support numbered conversion specifications (aka positional arguments) as per POSIX.1-2024
* Add additional input validation, error handling to all function calls, more bounds/buffer overrun checks
* Native language support for error / diagnostic messages


## Standards

Relative to the POSIX standard, this version:
* Supports "C" (i.e. `"%C"` format) as a conversion specifier for outputing the first single wide character of the corresponding command argument rather than the first byte of the argument's encoded form as required by the "c" conversion specifier (i.e. `"%c"` format).

* Supports "S" (i.e. `"%S"` format) as a conversion specifier for outputing operands like the "s" conversion specifier (i.e. `"%s"` format) with the difference that "S" converts multibyte sequences to wide character strings before printing.  This allows specifying format width and precision in terms of wide characters rather than bytes.

* Supports "Q" (i.e. `"%Q"` format) as the wide character string version of the "b" conversion specifier (i.e. the `"%b"` format) for processing escape sequences in command arguments like the "b" specifier  but outputing as a wide character string like "S" above.  When specifying left- or right-aligned output of command arguments that may contain non-ASCII characters (including those generated by the \u and \U escape sequenes below) via formats with specified width or precision, this is more likely to produce the desired result.

* Enables both the `\ddd` notation and the `\0ddd` notation in the format operand as well as in arguments associated with `"%b"` (and `"%Q"`) conversions while the standard specifies only `\ddd` for the format operand and only `\0ddd` for the argument associated with the `"%b"` operand.

* Supports `\uXXXX` and `\UXXXXXXXX` escape sequences in both the format operand as well as arguments associated with "b" and "Q" conversion specifiers for generating characters in the current character set and encoding that correspond to specific Unicode codepoints.  In a UTF-8 locale, this will just output the corresponding UTF-8 sequence of that codepoint.  Values are specified using hexidecimal numbers and the \u notation may be used for any valid Unicode codepoint up to `U+FFFF`.  The \U notation may be used for codepoints up to `U+10FFFF`.

* Does not support numbered argument conversions, which were added to POSIX.1-2024:
https://pubs.opengroup.org/onlinepubs/9799919799/utilities/printf.html


## Learnings

* A reminder of what academics already know:
  - Nothing teaches something like implementing it

  - Creating something and explaining the design is a journey with its own rewards

* A reminder of what professional programmers already know:
  - Even relatively simple utilities such as this involve lots of design decisions, many of which are made on the fly and may take longer to document than they did to make

  - Accomidating new specifications (or a new understanding of the original specifications) often leads to cascading design changes and/or slow, cumbersome workarounds

  - The current C and POSIX standards remain incomplete as far as support for writing non-trivial highly portable, any-locale applications

  - Writing fast and safe code still requires a lot of work (and thus time)

* A platform's character set is so fundemental that its definition is like the definition of what "is" is.

* A set of functions is not an API

* The classic IETF RFC process should be the standard for setting standards
