# cJSON

Ultralightweight JSON parser in ANSI C by Dave Gamble and cJSON contributors,
MIT license (see [LICENSE](LICENSE)).

- Upstream: https://github.com/DaveGamble/cJSON
- Version: 1.7.19 (tag `v1.7.19`), `cJSON.c` and `cJSON.h` unchanged
  (only a trailing space removed in a comment of `cJSON.h`)

Built with `-DENABLE_LOCALES` (`src/serviceapp/Makefile.am`), as cJSON's own
CMake build does: numbers are read with `strtod()`, which follows the locale's
decimal point, and enigma2 runs in the user's locale (',' in German). Without
it every number with a fraction (gstplayer2's length and position) fails to
parse there.

ServiceApp parses the JSON lines of exteplayer3 and gstplayer with it. To
update: copy `cJSON.c`, `cJSON.h` and `LICENSE` of a new release tag here and
change the version above.
