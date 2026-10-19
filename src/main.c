#include "json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *stream) {
    fputs("Usage: j1 [--pretty | --check] [FILE | -]\n"
          "Read JSON from FILE or standard input; emit compact JSON by default.\n", stream);
}
int main(int argc, char **argv) {
    bool pretty = false, check = false;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) { usage(stdout); return 0; }
        if (!strcmp(argv[i], "--pretty")) pretty = true;
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!path && (argv[i][0] != '-' || !strcmp(argv[i], "-"))) path = argv[i];
        else { usage(stderr); return 2; }
    }
    if (pretty && check) { usage(stderr); return 2; }
    FILE *input = path && strcmp(path, "-") ? fopen(path, "rb") : stdin;
    if (!input) { perror(path); return 2; }
    size_t length = 0, capacity = 4096;
    char *data = malloc(capacity);
    if (!data) { fputs("Out of memory\n", stderr); if (input != stdin) fclose(input); return 2; }
    for (;;) {
        size_t n = fread(data + length, 1, capacity - length, input);
        length += n;
        if (ferror(input)) { perror("Read failed"); free(data); if (input != stdin) fclose(input); return 2; }
        if (feof(input)) break;
        if (length == capacity) {
            char *next = capacity <= SIZE_MAX / 2 ? realloc(data, capacity * 2) : NULL;
            if (!next) { fputs("Input too large or out of memory\n", stderr); free(data); if (input != stdin) fclose(input); return 2; }
            data = next;
            capacity *= 2;
        }
    }
    if (input != stdin) fclose(input);
    json_error error;
    json_value *root = json_parse(data, length, &error);
    if (!root) {
        size_t line = 1, column = 1;
        for (size_t i = 0; i < error.offset && i < length; i++) {
            if (data[i] == '\n') { line++; column = 1; } else column++;
        }
        fprintf(stderr, "%s:%zu:%zu: %s (byte %zu)\n", path ? path : "<stdin>", line, column, error.message, error.offset);
        free(data);
        return 1;
    }
    free(data);
    int status = 0;
    if (!check) {
        size_t output_length;
        char *output = json_stringify(root, pretty, &output_length);
        if (!output) { fputs("Out of memory\n", stderr); status = 2; }
        else {
            if (fwrite(output, 1, output_length, stdout) != output_length || putchar('\n') == EOF || fflush(stdout) == EOF) {
                fputs("Write failed\n", stderr); status = 2;
            }
            free(output);
        }
    }
    json_free(root);
    return status;
}
