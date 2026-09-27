#define _POSIX_C_SOURCE 200809L
#include "tarpit.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
    if (argc > 3) {
        fprintf(stderr, "usage: %s [token] [corpus-file]\n", argv[0]);
        return 2;
    }

    const char *token = argc > 1 ? argv[1] : "";

    /* supply tarpit_open* with a fixed seed for deterministic generation */
    tarpit_generator *g = argc > 2
        ? tarpit_open(argv[2], UINT64_C(0x9a5f82cb), NULL)
        : tarpit_open_embedded(UINT64_C(0x9a5f82cb), NULL);

    if (!g) { perror("tarpit_open"); return 1; }

    tarpit_page page = {0};
    /* the token is used to control which content gets generated */
    if (tarpit_generate(g, token, (uint64_t)time(NULL), &page) < 0) {
        perror("tarpit_generate");
        tarpit_close(g);
        return 1;
    }
    printf("Suggested delay: %u ms\nNext token: %s\nLink label: %s\n\n%s",
           page.delay_ms, page.next_token, page.link_label, page.text);
    tarpit_page_free(&page);
    tarpit_close(g);
    return 0;
}
