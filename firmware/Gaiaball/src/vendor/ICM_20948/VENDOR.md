# Vendored: SparkFun ICM-20948 Arduino Library

- Version: **v1.3.1** (tag commit `242786c07bbe0a6cf06c2fc788e42123a1e6fbe1`)
- Origin: https://github.com/sparkfun/SparkFun_ICM-20948_ArduinoLibrary
- Contents: the library's `src/` directory only, copied as-is (`ICM_20948.h/.cpp` and
  `util/`, including the two alternate DMP firmware images under `util/` subfolders,
  which are not compiled). Examples, `library.properties` and `keywords.txt` are omitted.
- License: MIT (SparkFun code license, see `License.md` in the upstream repository).

## Local edits

1. `util/ICM_20948_C.h`: `#define ICM_20948_USE_DMP` enabled (upstream ships it
   commented out). Marked with `GAIABALL EDIT`.

No other changes. It compiles warning-free under the ESP32 Arduino core with
`-Wall -Wextra` (arduino-cli `--warnings all`).

## Updating

Replace the files from a newer tag's `src/`, re-apply the edit above, update this file.
