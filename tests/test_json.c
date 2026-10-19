#include "json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static json_value *parse(const char *s) {
    json_error e;
    json_value *v = json_parse(s, strlen(s), &e);
    if (!v) fprintf(stderr, "Unexpected error at %zu: %s in %s\n", e.offset, e.message, s);
    assert(v && !e.message);
    return v;
}
static void roundtrip(const char *s) {
    json_value *v = parse(s);
    for (int pretty = 0; pretty < 2; pretty++) {
        size_t n;
        char *encoded = json_stringify(v, pretty != 0, &n);
        assert(encoded && strlen(encoded) == n);
        json_value *again = parse(encoded);
        char *second = json_stringify(again, pretty != 0, NULL);
        assert(second && !strcmp(encoded, second));
        free(second);
        free(encoded);
        json_free(again);
    }
    json_free(v);
}
int main(void) {
    const char *valid[] = {
        "null", "true", "false", "0", "-0", "123456789012345678901234567890",
        "-12.34e+56", "1E-99999", "1e99999", "[]", "{}", "\"\"",
        " \r\n\t[null,true,false,{},[],1] \n", "{\"x\":1,\"x\":2}",
        "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"",
        "\"\\u0000\\u00e9\\u20ac\\ud83d\\ude00\"", "\"é€😀\""
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) roundtrip(valid[i]);
    const char *invalid[] = {
        "", " ", "nul", "True", "undefined", "+1", "01", "-01", "-", ".1",
        "1.", "1e", "1e+", "NaN", "Infinity", "true false", "nullx", "[1,]",
        "[,1]", "[1 2]", "[", "{", "{x:1}", "{\"a\" 1}", "{\"a\":}",
        "{\"a\":1,}", "\"unterminated", "\"\\x\"", "\"\n\"", "\"\\u12\"",
        "\"\\uZZZZ\"", "\"\\ud800\"", "\"\\udc00\"", "\"\\ud800\\u0041\"",
        "\"\xc0\x80\"", "\"\xed\xa0\x80\"", "\"\xf4\x90\x80\x80\"",
        "\"\xe0\x80\x80\"", "\"\x80\"", "\"\xc2\"", "\vnull", "/*x*/null"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        json_error e;
        json_value *v = json_parse(invalid[i], strlen(invalid[i]), &e);
        assert(!v && e.message && e.offset <= strlen(invalid[i]));
    }
    json_value *v = parse("{\"a\\u0000b\":\"x\\u0000y\",\"n\":12345678901234567890,\"a\":[true,null],\"n\":0}");
    assert(json_get_type(v) == JSON_OBJECT && json_size(v) == 4);
    size_t n;
    const char *s = json_get_string(json_get(v, "a\0b", 3), &n);
    assert(s && n == 3 && !memcmp(s, "x\0y", 3));
    s = json_key_at(v, 0, &n);
    assert(s && n == 3 && !memcmp(s, "a\0b", 3));
    assert(!strcmp(json_get_number(json_get(v, "n", 1)), "12345678901234567890"));
    const json_value *array = json_get(v, "a", 1);
    assert(json_size(array) == 2 && json_get_bool(json_at(array, 0)));
    assert(json_get_type(json_at(array, 1)) == JSON_NULL);
    assert(!json_at(array, 2) && !json_get(v, "missing", 7));
    assert(!json_get_string(array, &n) && n == 0);
    json_free(v);
    v = parse("\"\\u00e9\\u20ac\\ud83d\\ude00\"");
    s = json_get_string(v, &n);
    assert(n == 9 && !memcmp(s, "é€😀", n));
    json_free(v);
    char nested[515];
    memset(nested, '[', 256);
    memset(nested + 256, ']', 256);
    nested[512] = '\0';
    roundtrip(nested);
    memset(nested, '[', 257);
    memset(nested + 257, ']', 257);
    assert(!json_parse(nested, 514, NULL));
    const char raw[] = {'t', 'r', 'u', 'e'};
    v = json_parse(raw, sizeof(raw), NULL);
    assert(v && json_get_bool(v));
    json_free(v);
    assert(!json_parse("null\0x", 6, NULL));
    assert(!json_parse(NULL, 0, NULL));
    assert(!json_stringify(NULL, false, &n) && n == 0);
    json_free(NULL);
    /* Exercise partial parses and cleanup for every prefix. */
    const char *complex = "{\"key\":[1,true,{\"unicode\":\"\\ud83d\\ude00\"}]}";
    for (size_t i = 0; i < strlen(complex); i++) {
        v = json_parse(complex, i, NULL);
        assert(!v);
    }
    puts("All JSON tests passed.");
    return 0;
}
