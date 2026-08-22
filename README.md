# Soap Films

A small Android viewer for minimal surfaces and soap films.

The first milestone is deliberately simple: start on one interesting surface,
render immediately, and let the surface be handled as an object in space.
No wire editing or surface evolution is part of this slice yet.

## First interaction

- one finger: orbit the surface
- two fingers, move together: pan in the current camera plane
- two fingers, change separation: pinch zoom
- lifting one finger from a two-finger gesture ends that gesture; a fresh touch
  begins the next gesture

The native layer consumes Android `AMotionEvent` directly and owns only EGL,
GLES3, asset loading, and gesture-to-camera state. Surface mathematics lives in
Edriç (`.idric`) files and is compiled through the Idris/Edriç GLSL ES backend.

## Surfaces

Each rendered surface has one source file under `src/Surface/`.

Current first-frame set:

- `Catenoid.idric` — the canonical soap film between two parallel rings,
  truncated to the useful central patch
- `Helicoid.idric` — a second exact classical minimal surface, included so
  startup selection exercises a real surface catalog rather than a hard-coded
  single shader

`Common.idric` contains only camera/ray/shading helpers shared by those files.
The build discovers every `.idric` module containing a `glsles:fragment`
export, emits one fragment shader per playable surface, and writes
`app/src/main/assets/surfaces/index.txt`. The Android app picks one entry from
that index at process start.

The museum expansion rule is therefore file-driven: add a surface source with
a fragment export and it automatically becomes another startup choice. Keep
its provenance and exact/approximate status explicit. Nodal approximations of
triply periodic surfaces must be labelled as approximations rather than called
the exact minimal surface.

This fragment path is not intended to force every museum surface into implicit
ray marching. Parametric and Surface Evolver-backed entries can gain their own
representations behind the same camera interaction as the renderer grows.

## Edriç -> GLSL dogfood path

The shader backend currently lives in `isomorphisms/idris-shader-backend`.
Its normal executable is built against stock Idris 2. Soap deliberately builds
that same backend against the Edriç compiler API instead:

1. bootstrap `isomorphisms/Idric`
2. install Edriç's compiler API into its private bootstrap prefix
3. build `idris2-glsles` with that Edriç executable
4. feed the `.idric` surface files through the resulting code generator
5. package the generated GLSL ES 3.00 shaders as Android assets

Run:

```sh
scripts/build-edric-glsles.sh
scripts/build-shaders.sh
gradle --no-daemon :app:assembleDebug
```

The source shader API still spells its scalar type `Double`, even though the
backend emits GLSL `float`. Soap uses that existing contract for the first
frame. Treating 32-bit `Float` as the source-level scalar is a separate backend
dogfood issue rather than something this app should paper over.

## Deliberately deferred

- grabbing or bending wire
- re-running a physical minimal-surface solver after a wire edit
- switching surfaces from an on-screen catalog
- three-finger actions
- thin-film optics beyond the small view-angle colour cue in the first shader
- importing the full Ken Brakke/Virtual Minimal Surface Museum collections

The `Ken brackey/` directory remains upstream reference material. Native Soap
code does not modify upstream files in that directory.
