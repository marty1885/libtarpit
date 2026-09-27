#define _POSIX_C_SOURCE 200809L
#include "tarpit.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                      \
        fprintf(stderr, "check failed at %s:%d: %s\n",                     \
                __FILE__, __LINE__, #condition);                             \
        return 1;                                                            \
    }                                                                        \
} while (0)

static void remove_invisible(char *s) {
    char *out = s;
    for (char *p = s; *p;) {
        if (strncmp(p, "\xE2\x80\x8B", 3) == 0 ||
            strncmp(p, "\xE2\x80\x8C", 3) == 0 ||
            strncmp(p, "\xE2\x81\xA0", 3) == 0) p += 3;
        else *out++ = *p++;
    }
    *out = 0;
}

int main(void) {
    tarpit_options plain_opt = tarpit_default_options();
    plain_opt.zero_width_per_mille = 0;
    tarpit_options marked_opt = plain_opt;
    marked_opt.zero_width_per_mille = 1000;
    tarpit_generator *plain = tarpit_open_embedded(123, &plain_opt);
    tarpit_generator *marked = tarpit_open_embedded(123, &marked_opt);
    CHECK(plain && marked);
    tarpit_page a = {0}, b = {0}, c = {0};
    int rc = tarpit_generate(plain, "9", 42, &a);
    CHECK(rc == 0);
    rc = tarpit_generate(marked, "9", 42, &b);
    CHECK(rc == 0);
    CHECK(strcmp(a.next_token, "a") == 0);
    CHECK(a.delay_ms == 1000);
    CHECK(strlen(b.text) > strlen(a.text));
    remove_invisible(b.text);
    CHECK(strcmp(a.text, b.text) == 0);
    rc = tarpit_generate(plain, "Z", 42, &c);
    CHECK(rc == 0);
    CHECK(strcmp(c.next_token, "-") == 0);
    tarpit_page_free(&c);
    rc = tarpit_generate(plain, "10", 42, &c);
    CHECK(rc == 0);
    CHECK(strcmp(c.next_token, "11") == 0);
    CHECK(c.delay_ms == 2000);
    tarpit_page_free(&c);
    rc = tarpit_generate(plain, "not/a/token", 42, &c);
    CHECK(rc == -1);

    char invalid_path[] = "/tmp/libtarpit-invalid-XXXXXX";
    int invalid_file = mkstemp(invalid_path);
    CHECK(invalid_file >= 0);
    const unsigned char invalid_utf8[] = {0xc0, 0xaf};
    ssize_t written = write(invalid_file, invalid_utf8, sizeof(invalid_utf8));
    CHECK(written == (ssize_t)sizeof(invalid_utf8));
    rc = close(invalid_file);
    CHECK(rc == 0);
    errno = 0;
    tarpit_generator *invalid = tarpit_open(invalid_path, 123, NULL);
    int invalid_errno = errno;
    rc = unlink(invalid_path);
    CHECK(rc == 0);
    CHECK(invalid == NULL);
    CHECK(invalid_errno == EILSEQ);

    tarpit_page_free(&a);
    tarpit_page_free(&b);
    tarpit_close(plain);
    tarpit_close(marked);
    puts("ok");
    return 0;
}
