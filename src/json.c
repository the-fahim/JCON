#include "json.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 256

typedef struct { char *data; size_t length, capacity; } buffer;
typedef struct { char *data; size_t length; } string;
typedef struct { string key; json_value *value; } member;
struct json_value {
    json_type type;
    union {
        bool boolean;
        string text;
        struct { member *items; size_t length, capacity; } children;
    } as;
};
typedef struct {
    const unsigned char *input;
    size_t length, pos;
    json_error *error;
    bool failed;
} parser;

static bool append(buffer *b, const void *data, size_t n) {
    if (n > SIZE_MAX - b->length - 1) return false;
    size_t required = b->length + n + 1;
    if (required > b->capacity) {
        size_t cap = b->capacity ? b->capacity : 32;
        while (cap < required) {
            if (cap > SIZE_MAX / 2) { cap = required; break; }
            cap *= 2;
        }
        char *next = realloc(b->data, cap);
        if (!next) return false;
        b->data = next;
        b->capacity = cap;
    }
    if (n) memcpy(b->data + b->length, data, n);
    b->length += n;
    b->data[b->length] = '\0';
    return true;
}

static bool byte(buffer *b, unsigned char c) { return append(b, &c, 1); }
static void fail(parser *p, const char *message) {
    if (!p->failed && p->error) *p->error = (json_error){p->pos, message};
    p->failed = true;
}
static void whitespace(parser *p) {
    while (p->pos < p->length) {
        unsigned char c = p->input[p->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        p->pos++;
    }
}
static bool take(parser *p, unsigned char c) {
    if (p->pos < p->length && p->input[p->pos] == c) { p->pos++; return true; }
    return false;
}
static bool hex4(parser *p, uint32_t *result) {
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (p->pos == p->length) { fail(p, "incomplete Unicode escape"); return false; }
        unsigned char c = p->input[p->pos];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10u;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10u;
        else { fail(p, "invalid Unicode escape"); return false; }
        n = n * 16 + digit;
        p->pos++;
    }
    *result = n;
    return true;
}
static bool encode(buffer *b, uint32_t c) {
    if (c <= 0x7f) return byte(b, (unsigned char)c);
    if (c <= 0x7ff)
        return byte(b, (unsigned char)(0xc0 | (c >> 6))) && byte(b, (unsigned char)(0x80 | (c & 63)));
    if (c <= 0xffff)
        return byte(b, (unsigned char)(0xe0 | (c >> 12))) &&
               byte(b, (unsigned char)(0x80 | ((c >> 6) & 63))) && byte(b, (unsigned char)(0x80 | (c & 63)));
    return byte(b, (unsigned char)(0xf0 | (c >> 18))) && byte(b, (unsigned char)(0x80 | ((c >> 12) & 63))) &&
           byte(b, (unsigned char)(0x80 | ((c >> 6) & 63))) && byte(b, (unsigned char)(0x80 | (c & 63)));
}
static bool parse_string(parser *p, string *out) {
    buffer b = {0};
    if (!take(p, '"')) { fail(p, "expected string"); return false; }
    while (p->pos < p->length) {
        unsigned char c = p->input[p->pos++];
        if (c == '"') {
            if (!append(&b, "", 0)) goto oom;
            *out = (string){b.data, b.length};
            return true;
        }
        if (c < 0x20) { p->pos--; fail(p, "unescaped control character"); goto bad; }
        if (c == '\\') {
            if (p->pos == p->length) { fail(p, "incomplete escape"); goto bad; }
            c = p->input[p->pos++];
            switch (c) {
                case '"': case '\\': case '/': break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    uint32_t cp, low;
                    if (!hex4(p, &cp)) goto bad;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (!take(p, '\\') || !take(p, 'u')) { fail(p, "expected low surrogate"); goto bad; }
                        if (!hex4(p, &low)) goto bad;
                        if (low < 0xdc00 || low > 0xdfff) { fail(p, "invalid low surrogate"); goto bad; }
                        cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                        fail(p, "unpaired low surrogate"); goto bad;
                    }
                    if (!encode(&b, cp)) goto oom;
                    continue;
                }
                default: p->pos--; fail(p, "invalid escape"); goto bad;
            }
        } else if (c >= 0x80) {
            size_t start = p->pos - 1;
            unsigned count;
            uint32_t cp, minimum;
            if (c >= 0xc2 && c <= 0xdf) { count = 1; cp = c & 31u; minimum = 0x80; }
            else if (c >= 0xe0 && c <= 0xef) { count = 2; cp = c & 15u; minimum = 0x800; }
            else if (c >= 0xf0 && c <= 0xf4) { count = 3; cp = c & 7u; minimum = 0x10000; }
            else { p->pos = start; fail(p, "invalid UTF-8"); goto bad; }
            for (unsigned i = 0; i < count; i++) {
                if (p->pos == p->length || (p->input[p->pos] & 0xc0) != 0x80) {
                    fail(p, "invalid UTF-8 continuation"); goto bad;
                }
                cp = (cp << 6) | (p->input[p->pos++] & 63u);
            }
            if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
                p->pos = start; fail(p, "invalid UTF-8 code point"); goto bad;
            }
            if (!append(&b, p->input + start, count + 1u)) goto oom;
            continue;
        }
        if (!byte(&b, c)) goto oom;
    }
    fail(p, "unterminated string");
    goto bad;
oom:
    fail(p, "out of memory");
bad:
    free(b.data);
    return false;
}

void json_free(json_value *v) {
    if (!v) return;
    if (v->type == JSON_STRING || v->type == JSON_NUMBER) free(v->as.text.data);
    else if (v->type == JSON_ARRAY || v->type == JSON_OBJECT) {
        for (size_t i = 0; i < v->as.children.length; i++) {
            free(v->as.children.items[i].key.data);
            json_free(v->as.children.items[i].value);
        }
        free(v->as.children.items);
    }
    free(v);
}
static bool digit(parser *p) {
    return p->pos < p->length && p->input[p->pos] >= '0' && p->input[p->pos] <= '9';
}
static bool number(parser *p, string *out) {
    size_t start = p->pos;
    (void)take(p, '-');
    if (!take(p, '0')) {
        if (!digit(p)) { fail(p, "expected digit"); return false; }
        while (digit(p)) p->pos++;
    }
    if (take(p, '.')) {
        if (!digit(p)) { fail(p, "expected fractional digit"); return false; }
        while (digit(p)) p->pos++;
    }
    if (take(p, 'e') || take(p, 'E')) {
        if (!take(p, '+')) (void)take(p, '-');
        if (!digit(p)) { fail(p, "expected exponent digit"); return false; }
        while (digit(p)) p->pos++;
    }
    buffer b = {0};
    if (!append(&b, p->input + start, p->pos - start)) { fail(p, "out of memory"); return false; }
    *out = (string){b.data, b.length};
    return true;
}
static json_value *value(parser *p, unsigned depth) {
    whitespace(p);
    if (p->pos == p->length) { fail(p, "expected value"); return NULL; }
    json_value *v = calloc(1, sizeof(*v));
    if (!v) { fail(p, "out of memory"); return NULL; }
    unsigned char c = p->input[p->pos];
    if (c == '[' || c == '{') {
        if (depth >= MAX_DEPTH) { fail(p, "maximum nesting depth exceeded"); goto bad; }
        v->type = c == '[' ? JSON_ARRAY : JSON_OBJECT;
        unsigned char close = c == '[' ? ']' : '}';
        p->pos++;
        whitespace(p);
        if (take(p, close)) return v;
        for (;;) {
            member m = {0};
            if (v->type == JSON_OBJECT) {
                if (!parse_string(p, &m.key)) goto bad;
                whitespace(p);
                if (!take(p, ':')) { free(m.key.data); fail(p, "expected colon"); goto bad; }
            }
            m.value = value(p, depth + 1);
            if (!m.value) { free(m.key.data); goto bad; }
            if (v->as.children.length == v->as.children.capacity) {
                size_t cap = v->as.children.capacity ? v->as.children.capacity : 4;
                member *next = NULL;
                if (cap <= SIZE_MAX / 2 / sizeof(*next)) {
                    cap *= 2;
                    next = realloc(v->as.children.items, cap * sizeof(*next));
                }
                if (!next) { free(m.key.data); json_free(m.value); fail(p, "out of memory"); goto bad; }
                v->as.children.items = next;
                v->as.children.capacity = cap;
            }
            v->as.children.items[v->as.children.length++] = m;
            whitespace(p);
            if (take(p, close)) return v;
            if (!take(p, ',')) { fail(p, "expected comma or closing delimiter"); goto bad; }
            whitespace(p);
        }
    }
    if (c == '"') {
        v->type = JSON_STRING;
        if (!parse_string(p, &v->as.text)) goto bad;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        v->type = JSON_NUMBER;
        if (!number(p, &v->as.text)) goto bad;
    } else {
        const char *literal;
        if (c == 'n') { literal = "null"; v->type = JSON_NULL; }
        else if (c == 't') { literal = "true"; v->type = JSON_BOOL; v->as.boolean = true; }
        else if (c == 'f') { literal = "false"; v->type = JSON_BOOL; }
        else { fail(p, "expected JSON value"); goto bad; }
        size_t n = strlen(literal);
        if (p->length - p->pos < n || memcmp(p->input + p->pos, literal, n)) {
            fail(p, "invalid literal"); goto bad;
        }
        p->pos += n;
    }
    return v;
bad:
    json_free(v);
    return NULL;
}

json_value *json_parse(const char *input, size_t length, json_error *error) {
    if (error) *error = (json_error){0, NULL};
    parser p = {(const unsigned char *)input, length, 0, error, false};
    if (!input) { fail(&p, "input is NULL"); return NULL; }
    json_value *v = value(&p, 0);
    if (!v) return NULL;
    whitespace(&p);
    if (p.pos != length) { fail(&p, "unexpected trailing data"); json_free(v); return NULL; }
    return v;
}
json_type json_get_type(const json_value *v) { return v->type; }
bool json_get_bool(const json_value *v) { return v && v->type == JSON_BOOL && v->as.boolean; }
const char *json_get_number(const json_value *v) { return v && v->type == JSON_NUMBER ? v->as.text.data : NULL; }
const char *json_get_string(const json_value *v, size_t *length) {
    bool ok = v && v->type == JSON_STRING;
    if (length) *length = ok ? v->as.text.length : 0;
    return ok ? v->as.text.data : NULL;
}
size_t json_size(const json_value *v) {
    return v && (v->type == JSON_ARRAY || v->type == JSON_OBJECT) ? v->as.children.length : 0;
}
const json_value *json_at(const json_value *v, size_t index) {
    return index < json_size(v) ? v->as.children.items[index].value : NULL;
}
const char *json_key_at(const json_value *v, size_t index, size_t *length) {
    bool ok = v && v->type == JSON_OBJECT && index < json_size(v);
    if (length) *length = ok ? v->as.children.items[index].key.length : 0;
    return ok ? v->as.children.items[index].key.data : NULL;
}
const json_value *json_get(const json_value *v, const char *key, size_t n) {
    if (!v || v->type != JSON_OBJECT || (!key && n)) return NULL;
    for (size_t i = 0; i < json_size(v); i++) {
        member *m = &v->as.children.items[i];
        if (m->key.length == n && (!n || !memcmp(m->key.data, key, n))) return m->value;
    }
    return NULL;
}

static bool quote(buffer *b, string s) {
    static const char hex[] = "0123456789abcdef";
    if (!byte(b, '"')) return false;
    for (size_t i = 0; i < s.length; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if (c == '"' || c == '\\') {
            if (!byte(b, '\\') || !byte(b, c)) return false;
        } else if (c < 0x20) {
            char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            if (!append(b, escaped, sizeof(escaped))) return false;
        } else if (!byte(b, c)) return false;
    }
    return byte(b, '"');
}
static bool indent(buffer *b, unsigned depth) {
    if (!byte(b, '\n')) return false;
    for (unsigned i = 0; i < depth; i++) if (!append(b, "  ", 2)) return false;
    return true;
}
static bool emit(buffer *b, const json_value *v, bool pretty, unsigned depth) {
    switch (v->type) {
        case JSON_NULL: return append(b, "null", 4);
        case JSON_BOOL: return v->as.boolean ? append(b, "true", 4) : append(b, "false", 5);
        case JSON_NUMBER: return append(b, v->as.text.data, v->as.text.length);
        case JSON_STRING: return quote(b, v->as.text);
        case JSON_ARRAY: case JSON_OBJECT: {
            bool object = v->type == JSON_OBJECT;
            if (!byte(b, object ? '{' : '[')) return false;
            for (size_t i = 0; i < json_size(v); i++) {
                member *m = &v->as.children.items[i];
                if (i && !byte(b, ',')) return false;
                if (pretty && !indent(b, depth + 1)) return false;
                if (object && (!quote(b, m->key) || !byte(b, ':') || (pretty && !byte(b, ' ')))) return false;
                if (!emit(b, m->value, pretty, depth + 1)) return false;
            }
            if (pretty && json_size(v) && !indent(b, depth)) return false;
            return byte(b, object ? '}' : ']');
        }
    }
    return false;
}
char *json_stringify(const json_value *v, bool pretty, size_t *length) {
    buffer b = {0};
    if (length) *length = 0;
    if (!v || !emit(&b, v, pretty, 0)) { free(b.data); return NULL; }
    if (length) *length = b.length;
    return b.data;
}
