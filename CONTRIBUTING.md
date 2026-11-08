# Contributing

Please include your OS, game region/title ID, whether it is a player or manager
career, steps to reproduce, and the error text. Do not attach saves publicly: they
can contain account identifiers. Never attach API keys or console credentials.

Build with CMake, run the Python reference tests and `tests/native_check.py` against
the native executable. Test fixtures must be synthetic. No game binaries or
proprietary assets belong in the repository.

Keep edits fixed-size and restricted to supported fields. Every new write path
must validate source integrity, preserve non-target bytes and account identity,
and recalculate both CRC levels. Document validation limits for new save formats.

Before shipping binaries, inspect the CI artifacts on all claimed platforms,
include dependency notices, and verify packaging on a clean machine. Apple
notarization and Windows code signing require maintainer credentials; do not
commit them.
