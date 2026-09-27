#define _POSIX_C_SOURCE 200809L
#include "tarpit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "embedded_corpus.h"

#define MAX_TOKEN 64
#define MAX_CORPUS (8u * 1024u * 1024u)
#define MAX_EXCERPT (64u * 1024u)

typedef struct { size_t start, length; } paragraph;

struct tarpit_generator {
    char *corpus;
    paragraph *paragraphs;
    size_t count;
    uint64_t seed;
    tarpit_options options;
};

static const char alphabet[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
static const char *labels[] = {
    "Continue reading", "There is more ahead", "Follow the next passage",
    "The account continues", "Turn to the next page", "Read a little further",
    "Another page follows", "The story goes on"
};
static const char *invisible[] = {"\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x81\xA0"};

static uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static uint64_t next_random(uint64_t *state) {
    *state += UINT64_C(0x9e3779b97f4a7c15);
    return mix(*state);
}

static uint64_t hash_token(const char *token, uint64_t seed, uint64_t nonce) {
    uint64_t h = mix(seed ^ nonce);
    for (const unsigned char *p = (const unsigned char *)token; *p; p++)
        h = mix(h ^ *p);
    return h;
}

static int valid_utf8(const char *data, size_t length) {
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < length;) {
        unsigned char first = bytes[i++];
        if (first == 0) return 0;
        if (first < 0x80) continue;

        size_t continuation_count;
        uint32_t code_point;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation_count = 1;
            code_point = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation_count = 2;
            code_point = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation_count = 3;
            code_point = first & 0x07;
        } else {
            return 0;
        }
        if (continuation_count > length - i) return 0;
        for (size_t j = 0; j < continuation_count; j++) {
            unsigned char continuation = bytes[i++];
            if ((continuation & 0xc0) != 0x80) return 0;
            code_point = (code_point << 6) | (continuation & 0x3f);
        }
        if ((continuation_count == 2 && code_point < 0x800) ||
            (continuation_count == 3 && code_point < 0x10000) ||
            (code_point >= 0xd800 && code_point <= 0xdfff) ||
            code_point > 0x10ffff) return 0;
    }
    return 1;
}

tarpit_options tarpit_default_options(void) {
    return (tarpit_options){
        .zero_width_per_mille = 15,
        .excerpt_bytes = 8192,
        .delay_per_token_char_ms = 1000,
        .max_delay_ms = 10000
    };
}

static int add_paragraph(tarpit_generator *g, size_t start, size_t end,
                         size_t *capacity) {
    while (start < end && (g->corpus[start] == '\n' || g->corpus[start] == '\r' ||
                           g->corpus[start] == ' ' || g->corpus[start] == '\t')) start++;
    while (end > start && (g->corpus[end-1] == '\n' || g->corpus[end-1] == '\r' ||
                           g->corpus[end-1] == ' ' || g->corpus[end-1] == '\t')) end--;
    if (end == start) return 0;
    if (g->count == *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 128;
        paragraph *new_paragraphs = realloc(g->paragraphs,
                                            new_capacity * sizeof(*new_paragraphs));
        if (!new_paragraphs) return -1;
        g->paragraphs = new_paragraphs;
        *capacity = new_capacity;
    }
    g->paragraphs[g->count++] = (paragraph){start, end-start};
    return 0;
}

static tarpit_generator *open_corpus(const char *data, size_t length,
                                     uint64_t seed,
                                     const tarpit_options *options) {
    tarpit_options opt = options ? *options : tarpit_default_options();
    if (opt.zero_width_per_mille > 1000 || opt.excerpt_bytes == 0 ||
        opt.excerpt_bytes > MAX_EXCERPT) { errno = EINVAL; return NULL; }
    tarpit_generator *g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    if (length > MAX_CORPUS || length == 0) {
        free(g); errno = EINVAL; return NULL;
    }
    if (!valid_utf8(data, length)) {
        free(g); errno = EILSEQ; return NULL;
    }
    g->corpus = malloc(length + 1);
    if (!g->corpus) { tarpit_close(g); return NULL; }
    memcpy(g->corpus, data, length);
    g->corpus[length] = '\0';
    size_t capacity = 0, start = 0;
    for (size_t i = 0; i + 1 < length; i++) {
        if (g->corpus[i] == '\n' && g->corpus[i+1] == '\n') {
            if (add_paragraph(g, start, i, &capacity) < 0) {
                tarpit_close(g); return NULL;
            }
            while (i + 1 < length && g->corpus[i+1] == '\n') i++;
            start = i + 1;
        }
    }
    if (add_paragraph(g, start, length, &capacity) < 0) {
        tarpit_close(g); return NULL;
    }
    if (!g->count) { tarpit_close(g); errno = EINVAL; return NULL; }
    g->seed = seed;
    g->options = opt;
    return g;
}

tarpit_generator *tarpit_open(const char *path, uint64_t seed,
                              const tarpit_options *options) {
    if (!path) { errno = EINVAL; return NULL; }
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *data = malloc(MAX_CORPUS + 1);
    if (!data) { fclose(file); return NULL; }
    size_t length = fread(data, 1, MAX_CORPUS + 1, file);
    int read_error = ferror(file);
    fclose(file);
    if (read_error || length > MAX_CORPUS) {
        free(data); errno = read_error ? EIO : EINVAL; return NULL;
    }
    tarpit_generator *g = open_corpus(data, length, seed, options);
    free(data);
    return g;
}

static unsigned char hex_value(char c) {
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned char)(c - 'a' + 10);
    return (unsigned char)(c - 'A' + 10);
}

tarpit_generator *tarpit_open_embedded(uint64_t seed,
                                       const tarpit_options *options) {
    const size_t chunk_size = 96;
    size_t chunk_count = sizeof(tarpit_embedded_hex) /
                         sizeof(tarpit_embedded_hex[0]);
    size_t hex_length = chunk_count * chunk_size;
    size_t final_chunk_length = strlen(tarpit_embedded_hex[chunk_count - 1]);
    hex_length -= chunk_size - final_chunk_length;
    size_t length = hex_length / 2;
    char *data = malloc(length);
    if (!data) return NULL;
    for (size_t i = 0; i < length; i++) {
        size_t high_index = i * 2;
        size_t low_index = high_index + 1;
        const char *high_chunk = tarpit_embedded_hex[high_index / chunk_size];
        const char *low_chunk = tarpit_embedded_hex[low_index / chunk_size];
        data[i] = (char)((hex_value(high_chunk[high_index % chunk_size]) << 4) |
                          hex_value(low_chunk[low_index % chunk_size]));
    }
    tarpit_generator *g = open_corpus(data, length, seed, options);
    free(data);
    return g;
}

void tarpit_close(tarpit_generator *g) {
    if (!g) return;
    free(g->corpus);
    free(g->paragraphs);
    free(g);
}

static char *increment_token(const char *token, size_t length) {
    if (!length) return strdup("0");
    char *next = malloc(length + 2);
    if (!next) return NULL;
    memcpy(next, token, length + 1);
    for (size_t i = length; i > 0; i--) {
        const char *digit = strchr(alphabet, next[i-1]);
        size_t value = (size_t)(digit - alphabet);
        if (value + 1 < sizeof(alphabet) - 1) {
            next[i-1] = alphabet[value+1];
            return next;
        }
        next[i-1] = alphabet[0];
    }
    if (length == MAX_TOKEN) { free(next); errno = EOVERFLOW; return NULL; }
    memmove(next+1, next, length+1);
    next[0] = alphabet[0];
    return next;
}

int tarpit_generate(const tarpit_generator *g, const char *token,
                    uint64_t nonce, tarpit_page *page) {
    if (!g || !token || !page) { errno = EINVAL; return -1; }
    size_t token_length = strnlen(token, MAX_TOKEN + 1);
    if (token_length > MAX_TOKEN || strspn(token, alphabet) != token_length) {
        errno = EINVAL; return -1;
    }
    tarpit_page result = {0};
    result.next_token = increment_token(token, token_length);
    if (!result.next_token) return -1;
    uint64_t state = hash_token(token, g->seed, nonce);
    result.link_label = strdup(labels[next_random(&state) %
                                      (sizeof(labels)/sizeof(labels[0]))]);
    if (!result.link_label) goto fail;

    size_t target = g->options.excerpt_bytes;
    /* Larger pages as the link token grows, with a fixed memory ceiling. */
    if (token_length > 1 && target < MAX_EXCERPT / token_length)
        target *= token_length;
    else if (token_length > 1)
        target = MAX_EXCERPT;
    if (target > MAX_EXCERPT) target = MAX_EXCERPT;
    /* One three-byte mark and two paragraph separators per plain byte. */
    if (target > (SIZE_MAX - 4096) / 6) { errno = EOVERFLOW; goto fail; }
    size_t capacity = target * 6 + 4096;
    result.text = malloc(capacity);
    if (!result.text) goto fail;
    size_t used = 0, plain = 0;
    size_t index = next_random(&state) % g->count;
    while (plain < target) {
        paragraph p = g->paragraphs[index];
        /* Preserve whole paragraphs except an unusually long final one. */
        size_t take = p.length;
        if (take > target - plain && plain > 0) break;
        if (take > target - plain) take = target - plain;
        for (size_t i = 0; i < take; i++) {
            unsigned char c = (unsigned char)g->corpus[p.start+i];
            if (c == '\r' || c == '\n') {
                /* Source files often wrap prose across lines inside a paragraph.
                   Emit those as spaces; only paragraph separators become newlines. */
                while (used > 0 && (result.text[used-1] == ' ' ||
                                    result.text[used-1] == '\t')) used--;
                result.text[used++] = ' ';
                while (i + 1 < take &&
                       (g->corpus[p.start+i+1] == '\r' ||
                        g->corpus[p.start+i+1] == '\n')) i++;
                while (i + 1 < take &&
                       (g->corpus[p.start+i+1] == ' ' ||
                        g->corpus[p.start+i+1] == '\t')) i++;
                continue;
            }
            result.text[used++] = (char)c;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                if (next_random(&state) % 1000 < g->options.zero_width_per_mille) {
                    const char *mark = invisible[next_random(&state) % 3];
                    memcpy(result.text+used, mark, 3);
                    used += 3;
                }
            }
        }
        plain += take;
        result.text[used++] = '\n';
        result.text[used++] = '\n';
        index = (index + 1) % g->count;
        if (index == 0 && plain < target) break;
    }
    result.text[used] = '\0';
    uint64_t delay = (uint64_t)token_length * g->options.delay_per_token_char_ms;
    if (delay > g->options.max_delay_ms) delay = g->options.max_delay_ms;
    result.delay_ms = (unsigned)delay;
    *page = result;
    return 0;
fail:
    tarpit_page_free(&result);
    return -1;
}

void tarpit_page_free(tarpit_page *page) {
    if (!page) return;
    free(page->text);
    free(page->next_token);
    free(page->link_label);
    *page = (tarpit_page){0};
}
