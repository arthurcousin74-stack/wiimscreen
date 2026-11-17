// Simulator: NVS settings kept in a text file (tools/sim: state directory), one namespace.
#pragma once
#include "Arduino.h"

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false);
    void end();
    String getString(const char* key, const String& def = String());
    bool getBool(const char* key, bool def = false);
    int getInt(const char* key, int def = 0);
    size_t putString(const char* key, const String& v);
    size_t putBool(const char* key, bool v);
    size_t putInt(const char* key, int v);
    bool remove(const char* key);
    bool clear();
    bool isKey(const char* key);
};
