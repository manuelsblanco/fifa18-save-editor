# Supported format and integrity

Input is a decrypted FIFA 18 **PS4** career ZIP, including `DATA`, `INDEX`, `NAME`
and `sce_sys/param.sfo` / `keystone`. A single containing directory is accepted.
The container must have three DB sections and matching checksums. Other FIFA
editions and native PS5 save formats are not supported.

## Database CRC

Each embedded DB starts with `44 42 00 08 00 00 00 00`. Size is at +8, table
count at +16. Table directory entries are four-byte codes and little-endian offsets.
CRC algorithm: polynomial `0x04c11db7`, initial `0xffffffff`, non-reflected,
no final XOR. Header, directory, table header and records carry separate CRCs.

Only the selected `players` fields are patched, in place. Integer bit offsets and
widths come from the file descriptors; the low range comes from the FIFA 18 schema.
For normal attributes, the visible value is stored value + 1. Visible 99 is encoded 98.
The players table's record CRC is recalculated after applying all edits.

## Whole-career CRC

The CRC is immediately after the null-terminated `SaveType_Career` marker. Its
covered range starts 24 bytes after the CRC position and ends at the end of `DATA`.
For the tested PS4 layout these positions are 78 and 102. Unlike DB CRCs, this is
standard reflected CRC32 (`zlib.crc32`). The original CRC must match before editing.

The exporter then recalculates the whole-career CRC. Updating only the DB table CRC
caused FIFA to reject a save even with a one-point attribute change.

## Account and encryption boundary

The editor never changes ACCOUNT_ID, INDEX, NAME, keystone or other SFO metadata.
It exports a **decrypted** ZIP. An external service or compatible console must
encrypt it again. The optional GarlicSaves client verifies the decrypted output
against the input before writing a console-ready encrypted ZIP.

GarlicSaves' API in the tested deployment required the eight raw ACCOUNT_ID bytes
as hex when submitting `account_id`; sending the displayed numeric account ID
produced a byte-reversed account. This may change: the client validates the output,
offers an explicit encoding option, and does not silently trust the folder name.

## Scope of validation

One real PS4 FIFA 18 player career was successfully edited with 12 attributes at
99 and loaded in FIFA 18 running on PS5. This is not proof that every region,
patch, career type or game save has an identical schema. Synthetic tests and a
Python reference reader additionally exercise offsets, range lows and CRCs.
Player names absent from `editedplayernames` appear as player IDs. Overall rating,
potential, appearance, accomplishments and progression logic are not edited.
