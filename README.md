# FIFA 18 Save Editor — Desktop

Native **C++17 / Qt 6** editor for decrypted FIFA 18 PS4 career saves, including
PS4 saves played on PS5. Local files stay local unless you explicitly use the
optional GarlicSaves API client. No account, save file or API key is bundled.

> Early release. The editing method was validated on one real PS4 player career
> running on PS5. Other regions and patches have not been exhaustively tested.
> Keep a working copy of your original save.

## Download v0.1.0 (pre-release)

Download the installers from [GitHub Releases — v0.1.0](https://github.com/manuelsblanco/fifa18-save-editor/releases/tag/v0.1.0):

- **Windows x64:** portable ZIP or EXE installer.
- **Linux x86_64:** DEB or RPM, built on Ubuntu 24.04 with Qt/libarchive dependencies.
- **macOS Apple Silicon:** DMG.

Windows and macOS binaries are unsigned. Read the release notes for compatibility
and validation details. GitHub's automatically generated source archives contain
source code; choose an attached installer to run the app.

**Español:** descarga los instaladores de la [versión preliminar v0.1.0](https://github.com/manuelsblanco/fifa18-save-editor/releases/tag/v0.1.0).

## Features

- Import a decrypted ZIP and verify the three embedded databases and career CRC.
- Detect the player-career character; search other players by edited name, ID or team.
- Edit 34 attributes with sliders and numeric inputs (1–99).
- Review original versus proposed values; reset a player; import/export proposals.
- Export a new decrypted ZIP with both table CRC and **whole-career CRC32** updated.
- Preserve all other save files and metadata; no automatic overall-rating calculation.
- Optional Python GarlicSaves client encrypts and then decrypts the output to verify
  account identity and game-file equality before producing the USB folder structure.

Normal roster names are not bundled. Players without an edited name appear as IDs.
This is not a native PS5-save decryptor, FIFA PC Cheat Engine table, or FUT editor.

## Use

1. Export FIFA 18 from the console's **Saved Data (PS4)** menu to USB.
2. Decrypt the matching save + `.bin` through your compatible console tool or
   [GarlicSaves](https://www.garlicsaves.com/). Include `sce_sys` in the result.
3. Launch the desktop app and open that **decrypted ZIP**.
4. Select the player, adjust attributes and export an edited ZIP to a new file.
5. Encrypt the ZIP as a **PS4** save with your account, manually or with the client below.
6. Extract the encrypted result to USB, preserving `PS4/SAVEDATA/<account>/<CUSA>/`.
7. Copy only the intended career to console storage and load it in FIFA 18.

The GUI currently uses Spanish labels. The native editor works offline and does
not need Python. The optional reference tests and GarlicSaves client use Python 3.10+.

## Build the desktop app

Dependencies: CMake 3.21+, C++17 compiler, Qt 6.4+ (Core, Widgets, Concurrent), libarchive.

### Ubuntu / Debian

```sh
sudo apt install build-essential cmake qt6-base-dev libarchive-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/fifa18-save-editor
```

Create distribution packages on a suitable distro (install `rpm` for the RPM generator):

```sh
cd build
cpack -G DEB
cpack -G RPM
```

Packages dynamically depend on Qt/libarchive. A DEB or RPM built on a newer distro
does not imply compatibility with older distros; use a matching build environment.

### Windows / macOS

Install Qt 6 and libarchive (vcpkg on Windows, Homebrew on macOS), point
`CMAKE_PREFIX_PATH` or the CMake toolchain at them, then use the same CMake build.
On Windows the binary is `fifa18-save-editor.exe`; macOS produces an `.app`.
The CI workflow defines Windows ZIP/EXE-installer, Linux DEB/RPM and macOS DMG builds.
Generated binaries must be tested before a release is advertised as supported.
Windows and macOS signing/notarization are not configured.

## Optional automatic encryption

This uploads the edited save to GarlicSaves, and submits a second decrypt job
to verify the returned account and save bytes. It requires a user-generated API
key with permission to submit jobs; service availability and terms may change.

```sh
python3 garlic_client.py edited-decrypted.zip encrypted-for-usb.zip
```

The key is requested with hidden input and is not saved. Alternatively:

```sh
python3 garlic_client.py edited-decrypted.zip encrypted-for-usb.zip --key-file /private/path/key.txt
```

Never put the key in this repository. The API's tested account encoding uses raw
SFO bytes; see [FORMAT.md](FORMAT.md). `--account-encoding numeric` supports a future
or different deployment. The result is verified before export in either mode.
API HTTP 403 can indicate a web-layer restriction, not necessarily an invalid key.
Check job IDs on the service before retrying after a connection failure to avoid
submitting duplicate jobs. The desktop GUI does not currently invoke the API client.

## Tests

```sh
python3 -m unittest discover -s tests -v
python3 tests/native_check.py build/fifa18-save-editor
```

Tests generate synthetic saves; no personal game files are committed. A separate
Python implementation cross-checks native no-op exports, bit-packed edits, range
lows, account preservation, CRCs and invalid-value rejection.

## Español

Editor de escritorio para carreras de FIFA 18 **de PS4**, también jugadas en PS5.
Abre el ZIP descifrado, selecciona al jugador, cambia atributos y exporta otra copia.
El programa corrige el CRC de la tabla y el CRC global de la carrera. Después debes
volver a cifrar el guardado antes de copiarlo a la consola. No modifica tu original.
La media y el potencial se muestran, pero no se editan ni recalculan.

Lee [FORMAT.md](FORMAT.md) para los detalles del formato y
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) para los créditos y dependencias.

License: MIT for this project's implementation. Not affiliated with EA or Sony.
