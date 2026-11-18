// Tests de la logique pure : pio test -e native
// All the .cpp files of test/test_core are built together. Each test_<module>.cpp holds the tests of one module and
// one run_<module>_tests() that runs them: add a test in its module's file, not here.
#include <unity.h>

#include "test_helpers.h"

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    run_fields_tests();
    run_wiim_api_tests();
    run_http_tests();
    run_json_tests();
    run_text_tests();
    run_layout_tests();
    run_upnp_tests();
    run_qobuz_deezer_tests();
    run_decisions_tests();
    run_appstate_tests();
    run_keys_tests();
    run_audio_tests();
    run_firmware_tests();
    run_robust_tests();
    return UNITY_END();
}
