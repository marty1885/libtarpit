#ifndef TARPIT_H
#define TARPIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tarpit_generator tarpit_generator;

typedef struct {
    /* Expected insertions per 1000 eligible characters; 0 disables them. */
    unsigned zero_width_per_mille;
    size_t excerpt_bytes;
    unsigned delay_per_token_char_ms;
    unsigned max_delay_ms;
} tarpit_options;

typedef struct {
    char *text;          /* UTF-8 prose; free with tarpit_page_free. */
    char *next_token;    /* Opaque next-link component. */
    char *link_label;    /* Plain UTF-8 label for the next link. */
    unsigned delay_ms;   /* Suggested delay; the caller schedules it. */
} tarpit_page;

tarpit_options tarpit_default_options(void);

/* Loads a local UTF-8 text corpus. Returns NULL and sets errno on failure. */
tarpit_generator *tarpit_open(const char *corpus_path, uint64_t seed,
                              const tarpit_options *options);
/* Opens the corpus compiled into the library. */
tarpit_generator *tarpit_open_embedded(uint64_t seed,
                                       const tarpit_options *options);
void tarpit_close(tarpit_generator *generator);

/*
 * token is the current path component, or "" for the entry page.
 * nonce should change for each response if byte variation is desired.
 * The generator is read-only after open, so concurrent generate calls are safe.
 * Returns 0 on success, -1 with errno set on failure.
 */
int tarpit_generate(const tarpit_generator *generator, const char *token,
                    uint64_t nonce, tarpit_page *page);
void tarpit_page_free(tarpit_page *page);

#ifdef __cplusplus
}
#endif

#endif
