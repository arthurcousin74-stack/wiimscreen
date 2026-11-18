#pragma once

// Version du firmware : numéro de build et commit, fournis par la CI
// (PLATFORMIO_BUILD_FLAGS, voir .github/workflows/firmware.yml) ; en local, tools/version.py
// donne « local-<commit git> » ; « dev » sans git.
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
