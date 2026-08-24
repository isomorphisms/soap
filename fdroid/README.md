# F-Droid release path

The Android project can produce the unsigned release APK F-Droid expects from public source and standard Android build inputs.

1. Keep `versionCode` and `versionName` in `app/build.gradle.kts` equal to the tagged public release.
2. Keep the normal Edriç/shader-generator CI green so the checked GLSL ES surface artifacts remain reproducible from `src/Surface/`.
3. Run the `F-Droid release build` workflow; it checks the source/artifact catalog, builds `assembleRelease`, verifies package identity and all three native ABIs, and retains the unsigned APK.
4. Tag the exact release commit `v<versionName>`.
5. Replace `FULL_COMMIT_HASH` in the metadata template with that commit and submit it as `metadata/org.isomorphisms.soap.yml` to fdroiddata.

F-Droid rebuilds and signs the application itself. The upstream unsigned APK is only a reproducibility gate.
