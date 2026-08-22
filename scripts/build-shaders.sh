#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
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

rm -rf "$source_root" "$output"
mkdir -p "$source_root/Shader" "$source_root/Surface" "$output"

# Keep the shader vocabulary authoritative in idris-shader-backend while giving
# Edric one source root containing both the backend API and Soap's .idric files.
cp "$backend_root/src/Shader/Source.idr" "$source_root/Shader/Source.idr"
cp "$repo_root/src/Surface/Common.idric" "$source_root/Surface/Common.idric"
cp "$repo_root/src/Surface/Catenoid.idric" "$source_root/Surface/Catenoid.idric"
cp "$repo_root/src/Surface/Helicoid.idric" "$source_root/Surface/Helicoid.idric"

"$compiler" --cg glsles \
    --source-dir "$source_root" \
    --output-dir "$output" \
    "$source_root/Surface/Catenoid.idric" \
    -o catenoid

"$compiler" --cg glsles \
    --source-dir "$source_root" \
    --output-dir "$output" \
    "$source_root/Surface/Helicoid.idric" \
    -o helicoid

printf '%s\n' catenoid.frag helicoid.frag > "$output/index.txt"

for shader in "$output"/*.frag; do
    grep -q '#version 300 es' "$shader"
    grep -q 'uniform float u_yaw;' "$shader"
    grep -q 'uniform float u_pitch;' "$shader"
    grep -q 'uniform float u_zoom;' "$shader"
    grep -q 'uniform vec2 u_pan;' "$shader"
done
