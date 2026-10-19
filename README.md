# J1 JSON parser

A dependency-free JSON parser and formatter written from scratch in C17, built
with Clang. Includes a reusable static library and a command-line tool.

## Build and run

```sh
make
printf '{"hello": [true, null, 42]}' | ./build/j1 --pretty
./build/j1 --check document.json
./build/j1 document.json
make test
make sanitize
```

The CLI reads a file or standard input (`-`), and emits compact JSON by default.
`--pretty` uses two-space indentation; `--check` validates without output.
Exit status is 0 for success, 1 for a parse failure, and 2 for CLI or I/O errors.
Parse diagnostics include a byte offset and one-based line and byte-column.
The entire input and parsed tree are held in memory.

## Library

```c
#include "json.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *input = "{\"answer\":42}";
    json_error error;
    json_value *root = json_parse(input, strlen(input), &error);
    if (!root) {
        fprintf(stderr, "%zu: %s\n", error.offset, error.message);
        return 1;
    }
    const json_value *answer = json_get(root, "answer", 6);
    puts(json_get_number(answer));
    json_free(root);
    return 0;
}
```

Compile consumers with `clang -std=c17 -Iinclude example.c build/libjson.a -o example`.
The public API is in `include/json.h`. Parsed values own their children; accessor
results remain valid until the root is freed. Free serialized strings with
`free()`. Input buffers need not be NUL-terminated and may be released after parsing.

## Behavior

- Supports objects, arrays, strings, numbers, booleans, and null, including scalar roots.
- Rejects trailing data, trailing commas, comments, malformed numbers, and invalid escapes.
- Validates raw UTF-8; decodes Unicode escapes and UTF-16 surrogate pairs to UTF-8.
  Rejects unpaired surrogates and unescaped control characters.
- Strings and object keys use explicit byte lengths and support embedded NUL bytes.
- Stores numbers as their original decimal text, preserving large integers and
  exponents without floating-point conversion or locale dependencies.
- Retains duplicate keys and insertion order; lookup returns the first match.
- Limits container nesting to 256 to bound recursive stack usage.
- Parsing uses no mutable global state. Allocation and parse failures clean up
  partial trees. Error messages have static storage.

`src/json.c` implements recursive-descent parsing, tree ownership, accessors,
and serialization. `src/main.c` handles CLI input and diagnostics.
Tests cover syntax rejection, Unicode, exact numbers, embedded NULs, nesting
limits, truncated inputs, serialization round trips, and CLI behavior.
