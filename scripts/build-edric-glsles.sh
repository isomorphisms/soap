#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
edric_root=${EDRIC_ROOT:-"$repo_root/deps/Idric"}
backend_root=${IDRIS_SHADER_BACKEND:-"$repo_root/deps/idris-shader-backend"}

if [ ! -x "$edric_root/edric" ]; then
    echo "missing Edric checkout at $edric_root" >&2
    exit 1
fi
if [ ! -f "$backend_root/backend.ipkg" ]; then
    echo "missing shader-backend checkout at $backend_root" >&2
    exit 1
fi

# Edric owns a pinned private Chez under .tools/bin. Keep it visible not only
# while the wrapper bootstraps the compiler, but also while this consumer runs
# the resulting compiler directly to install its API and build code generators.
PATH="$edric_root/.tools/bin:$PATH"
export PATH

"$edric_root/edric" bootstrap

edric="$edric_root/bootstrap-build/bin/idris2"
if [ ! -x "$edric" ]; then
    echo "Edric bootstrap did not produce $edric" >&2
    exit 1
fi

# The GLSL code generator imports Idris compiler modules. Install Edric's own
# compiler API into the same private bootstrap prefix, then compile the backend
# with that Edric executable. This is the dogfood boundary: no stock Idris
# compiler is used to parse the .idric surface files.
make -C "$edric_root" install-api \
    IDRIS2_BOOT="$edric" \
    PREFIX="$edric_root/bootstrap-build"

make -C "$backend_root" backend IDRIS2="$edric"

printf '%s\n' "$backend_root/build/exec/idris2-glsles"
