# CI only: turn compiler warnings into errors in the project's own files, not in third-party code.
#
# Used through the environment, so platformio.ini and local builds do not change:
#   PLATFORMIO_EXTRA_SCRIPTS=pre:tools/werror.py pio run -e sim
# PlatformIO appends this variable to the env's own extra_scripts. The build middleware below adds -Werror to
# every C/C++ file of the repository (src/, lib/core, test/, tools/sim, tools/preview, platform/pc), wherever it
# is compiled (project sources, the lib/core library, the unit tests). Files under .pio/ (lib_deps such as M5GFX,
# build outputs) and the PlatformIO packages (framework, toolchain, Unity) keep their flags.
# It only adds -Werror: the warnings checked are the ones each env already enables (see platformio.ini).
import os

Import("env")  # noqa: F821  (SCons)

ROOT = os.path.realpath(env.subst("$PROJECT_DIR"))  # noqa: F821
SKIP = (os.path.join(ROOT, ".pio") + os.sep,)


def own(path):
    path = os.path.realpath(path)
    return path.startswith(ROOT + os.sep) and not path.startswith(SKIP)


def werror(env, node):
    if not own(node.srcnode().get_abspath()):
        return node
    return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-Werror"])


env.AddBuildMiddleware(werror)  # noqa: F821
print("werror.py: -Werror on the project's own files")
