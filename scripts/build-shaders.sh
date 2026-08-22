#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
edric_root=${EDRIC_ROOT:-"$repo_root/deps/Idric"}
backend_root=${IDRIS_SHADER_BACKEND:-"$repo_root/deps/idris-shader-backend"}
compiler=${EDRIC_GLSLES:-"$backend_root/build/exec/idris2-glsles"}
output="$repo_root/app/src/main/assets/surfaces"
source_root="$repo_root/build/shader-source"

if [ ! -x "$compiler" ]; then
    echo "missing Edric GLSL compiler: $compiler" >&2
    exit 1
fi
if [ ! -f "$backend_root/src/Shader/Source.idr" ]; then
    echo "missing Shader.Source in $backend_root" >&2
    exit 1
fi
if [ ! -d "$edric_root/.tools/bin" ]; then
    echo "missing Edric private toolchain in $edric_root/.tools/bin" >&2
    exit 1
fi

# The registered GLSL compiler is itself an Edric/Chez executable. Keep the
# same pinned private Scheme visible when it is launched in this separate build
# step, not only while the compiler was bootstrapped.
PATH="$edric_root/.tools/bin:$PATH"
export PATH
LC_ALL=C
export LC_ALL

rm -rf "$source_root" "$output"
mkdir -p "$source_root/Shader" "$source_root/Surface" "$output"

# Keep the shader vocabulary authoritative in idris-shader-backend while giving
# Edric one source root containing both the backend API and every Soap surface
# module/helper.
cp "$backend_root/src/Shader/Source.idr" "$source_root/Shader/Source.idr"
for surface in "$repo_root"/src/Surface/*.idric; do
    cp "$surface" "$source_root/Surface/$(basename "$surface")"
done

: > "$output/index.txt"
playable=0
for surface in "$repo_root"/src/Surface/*.idric; do
    if ! grep -Fq '%export "glsles:fragment|' "$surface"; then
        continue
    fi

    file=$(basename "$surface")
    stem=${file%.idric}
    output_name=$(printf '%s' "$stem" | tr '[:upper:]' '[:lower:]')

    (
        cd "$source_root"
        "$compiler" --cg glsles \
            --source-dir . \
            --output-dir "$output" \
            "Surface/$file" \
            -o "$output_name"
    )

    printf '%s.frag\n' "$output_name" >> "$output/index.txt"
    playable=$((playable + 1))
done

if [ "$playable" -eq 0 ]; then
    echo "no playable glsles:fragment surfaces found under src/Surface" >&2
    exit 1
fi

for shader in "$output"/*.frag; do
    grep -q '#version 300 es' "$shader"
    grep -q 'uniform float u_yaw;' "$shader"
    grep -q 'uniform float u_pitch;' "$shader"
    grep -q 'uniform float u_zoom;' "$shader"
    grep -q 'uniform vec2 u_pan;' "$shader"
done
