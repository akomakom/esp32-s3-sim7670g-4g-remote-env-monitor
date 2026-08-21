# PlatformIO pre-build script: capture the current git short hash and write it to
# include/git_rev.h as `#define GIT_REV "<hash>[-dirty]"`. config.h appends this to
# FW_VERSION at build time. Generating a header (rather than a -D flag) is robust
# across the arduino+espidf/CMake build, and __has_include guards it if absent.
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)
import os
import subprocess


def git_rev():
    try:
        rev = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], stderr=subprocess.DEVNULL
        ).decode().strip()
        # Mark uncommitted working-tree changes so a field unit's version is honest.
        dirty = subprocess.call(
            ["git", "diff", "--quiet", "--ignore-submodules"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ) != 0
        return rev + ("-dirty" if dirty else "")
    except Exception:
        return "nogit"


rev = git_rev()
header = os.path.join(env["PROJECT_INCLUDE_DIR"], "git_rev.h")
content = '#pragma once\n#define GIT_REV "%s"\n' % rev

# Only rewrite when it changed, so we don't force a rebuild every invocation.
try:
    with open(header) as f:
        unchanged = f.read() == content
except OSError:
    unchanged = False
if not unchanged:
    with open(header, "w") as f:
        f.write(content)

print("git_rev.py: GIT_REV=%s -> %s" % (rev, header))
