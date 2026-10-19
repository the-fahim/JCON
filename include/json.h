#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>

/* Values own their children and strings. Accessor results are borrowed. */
typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} json_type;

typedef struct json_value json_value;
typedef struct {
    size_t offset; /* Zero-based byte offset. */
    const char *message; /* Static storage; never free. */
} json_error;

/* Input need not be NUL-terminated. On failure returns NULL and sets error.
 * Maximum container nesting is 256. Duplicate object keys are retained.
 * Numbers are stored verbatim, without floating-point precision loss. */
json_value *json_parse(const char *input, size_t length, json_error *error);
void json_free(json_value *value);

/* json_get_type requires a non-NULL value. Other accessors return false,
 * NULL or zero for the wrong type. Strings may contain embedded NUL bytes. */
json_type json_get_type(const json_value *value);
bool json_get_bool(const json_value *value);
const char *json_get_number(const json_value *value);
const char *json_get_string(const json_value *value, size_t *length);
size_t json_size(const json_value *value);
const json_value *json_at(const json_value *value, size_t index);
const char *json_key_at(const json_value *value, size_t index, size_t *length);
/* Lookup returns the first matching member; key_length supports NUL bytes. */
const json_value *json_get(const json_value *value, const char *key, size_t key_length);

/* Returns a malloc-owned, NUL-terminated string; NULL on allocation failure.
 * length excludes the terminator. Pretty mode uses two-space indentation. */
char *json_stringify(const json_value *value, bool pretty, size_t *length);

#endif
