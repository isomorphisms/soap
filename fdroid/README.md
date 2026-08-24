# F-Droid release path

The Android release can be rebuilt from public source without downloading compiler binaries during the build.

## Upstream gate

The `F-Droid release build` workflow checks out exact Idriç and shader-backend revisions plus the Chez Scheme 10.4.1 source tree and its submodules. It builds threaded Chez from source, bootstraps Idriç with that local executable, regenerates every GLSL ES surface, builds the unsigned Android release, and verifies package/version plus all three native ABIs.

## fdroiddata submission

The metadata template uses F-Droid `srclibs` for the same three source trees. Copy the three templates under `fdroid/srclibs/` into `fdroiddata/srclibs/` when submitting the app (unless equivalent srclib entries already exist), and copy `org.isomorphisms.soap.yml.template` to `fdroiddata/metadata/org.isomorphisms.soap.yml`.

Before submission:

1. Keep `versionCode` and `versionName` in `app/build.gradle.kts` equal to the tagged public release.
2. Run the upstream F-Droid release gate successfully.
3. Tag the exact integrated release commit `v<versionName>`.
4. Replace `FULL_COMMIT_HASH` in the metadata template with that 40-character commit SHA.
5. Run `fdroid lint org.isomorphisms.soap` and `fdroid build org.isomorphisms.soap` against the proposed fdroiddata files.

F-Droid rebuilds and signs the application itself. The upstream unsigned APK is only a reproducibility gate.
