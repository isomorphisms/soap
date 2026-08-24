# F-Droid release path

The upstream F-Droid validation build starts from the public repository and regenerates the Android surface shaders from pinned public Idriç/compiler sources before building the unsigned release APK.

1. Keep `versionCode` and `versionName` in `app/build.gradle.kts` equal to the tagged public release.
2. Keep the pinned `EDRIC_REF` and `SHADER_BACKEND_REF` in `.github/workflows/fdroid-build.yml` fixed for a release and update them deliberately.
3. Run the `F-Droid release build` workflow; it regenerates all playable surfaces, builds `assembleRelease`, verifies package identity and all three native ABIs, and retains the unsigned APK.
4. Tag the exact release commit `v<versionName>`.
5. Before submitting the fdroiddata recipe, represent the same two pinned source dependencies through F-Droid's source-preparation mechanism rather than relying on network access during the sandboxed Gradle build.

The YAML beside this file is an upstream metadata template, not a claim that fdroiddata already has source-library definitions for Idriç and the shader backend. F-Droid itself must be able to acquire those exact public source commits during source preparation.
