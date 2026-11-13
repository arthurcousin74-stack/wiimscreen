# Extra script PlatformIO : version du firmware pour les builds locaux.
# La CI passe -DFW_VERSION="<run>-<sha>" par PLATFORMIO_BUILD_FLAGS ; sinon on prend `git describe`
# (par exemple « local-1a2b3c4 » ou « local-1a2b3c4-modifie » si l'arbre de travail a des changements).
import os
import subprocess

Import("env")  # noqa: F821 (fourni par SCons)

if "FW_VERSION" not in os.environ.get("PLATFORMIO_BUILD_FLAGS", "") and \
        not any("FW_VERSION" in str(f) for f in env.get("CPPDEFINES", [])):  # noqa: F821
    try:
        sha = subprocess.check_output(["git", "describe", "--always", "--dirty=-modifie"],
                                      cwd=env.subst("$PROJECT_DIR"),  # noqa: F821
                                      stderr=subprocess.DEVNULL, text=True).strip()
        version = "local-" + sha
    except Exception:
        version = "dev"
    env.Append(CPPDEFINES=[("FW_VERSION", '\\"%s\\"' % version)])  # noqa: F821
