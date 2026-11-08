# Research and dependencies

The native engine and UI are an independent implementation. The field-name,
short-code and range-low mappings in `schema.json` were extracted from the FIFA 18
descriptor distributed by Sammy Griffiths' `fifa-career-save-parser` (ISC metadata):
https://github.com/sammygriffiths/fifa-career-save-parser

ISC permission notice for that source:

Copyright (c) Sammy Griffiths

Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.

Credit to xAranaktu for documenting FIFA 18 career databases, integer ranges,
and the career-wide CRC32:
https://soccergaming.com/forums/threads/fifa-18-save-editing-thread.6465621/
https://gist.github.com/xAranaktu/96e40cb2287372bbbe405408485a87e4

The table CRC layout was cross-checked against ViniMacacari/eafc-db-kit (MIT):
https://github.com/ViniMacacari/eafc-db-kit

Qt 6 is dynamically linked under its applicable LGPLv3/GPL/commercial terms.
libarchive is dynamically linked under its BSD-style licenses. Binary distributors
must include the corresponding dependency notices and comply with those terms.
No EA or Sony game binaries, decrypted saves, account identifiers or keys are included.
This project is not affiliated with EA, Sony, Qt or GarlicSaves.
