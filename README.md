# libtarpit

A small C text generator for application level crawl traps. Generating an endless a coherent excerpt, next-link token and a changing link label. Plus a a suggested response delay and anti-prefix compression. The design is based on observations of `gemini://buffering.party/tarpit/` that traps some of my crawlers (due to my own bugs) and I find the idea interesting.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/tarpit-demo 10
```

The build creates the `tarpit` library and `tarpit-demo`. It embeds `damned.txt` in the library, so the demo and applications can use the built-in corpus without a runtime data file. The demo prints one response; it does not wait for the suggested delay.

## Integration

```c
#include "tarpit.h"

tarpit_options opt = tarpit_default_options();
opt.zero_width_per_mille = 15; /* about 1.5% of ASCII letters */
tarpit_generator *gen = tarpit_open_embedded(application_seed, &opt);

tarpit_page page = {0};
if (tarpit_generate(gen, path_token, request_nonce, &page) == 0) {
    /* Schedule page.delay_ms without blocking an application worker. */
    /* Escape and render page.text, page.link_label, and page.next_token
       for your own protocol. Treat next_token as a single path component. */
    tarpit_page_free(&page);
}
tarpit_close(gen);
```

Pass a fresh `request_nonce` to vary both passage selection and invisible character positions between requests. Use a stable nonce to reproduce the same output. `tarpit_generate` is safe to call concurrently after `tarpit_open`. The token alphabet is `0-9a-zA-Z-_` and the maximum token length is 64. At the final length, counter overflow returns `EOVERFLOW` instead of growing without bound. The default excerpt is 8 KiB times token length, capped at 64 KiB. The delay is one second per token character, capped at ten seconds.

Zero width marks make copied text harder to compare or search and can affect screen readers. Set `zero_width_per_mille` to zero when you need plain text. The generator only inserts marks in its prose; it leaves tokens and link labels untouched so routing remains reliable. Serve this only to traffic you have chosen to tarpit, and keep normal pages accessible without these marks.

## Corpus

`damned.txt` is the text of Algernon Blackwood's *The Damned* (1914), obtained from [Project Gutenberg eBook 11074](https://www.gutenberg.org/ebooks/11074). The Project Gutenberg header and footer were removed. Check the applicable copyright rules before redistributing the corpus in your jurisdiction. The CMake build embeds this file into the library. You can also load a different UTF-8 text file at runtime with `tarpit_open`; it reads at most 8 MiB and splits passages at blank lines.
