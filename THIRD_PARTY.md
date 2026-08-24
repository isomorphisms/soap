# Third-party and referenced material

`LICENSE` applies only to material that this repository's contributors have authority to license. Third-party dependencies and reference material retain their own terms.

## Surface Evolver reference

The repository's Surface Evolver reference material is upstream work with its own terms. It is maintained as reference/provenance material and is not relicensed by this repository's GPL grant. It is not required by the current Android F-Droid build.

## Idriç and shader backend

The playable surface definitions under `src/Surface/` are maintained as Idriç source. The F-Droid validation workflow checks out exact public commits of `isomorphisms/Idric` and `isomorphisms/idris-shader-backend`, regenerates the GLSL ES files, and then builds the Android release. Those compiler repositories retain their own license terms.

## Platform and toolchain

Android SDK/NDK components, Gradle, CMake, native_app_glue, system libraries, and OpenGL ES interfaces are external to this repository and remain under their upstream terms.
