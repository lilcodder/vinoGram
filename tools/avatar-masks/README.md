# Expressive avatar geometry

vinoGram embeds the Material 3 Expressive indeterminate loading indicator's
seven shapes as a personal **profile-only** mask for Telegram account `1842426667`.
Other users can select a circle, rounded square or flower. Chat avatars keep
their normal rendering. Shape synchronization and offline caching are documented
in `tools/avatar-service/README.md`; no image is uploaded to that service.

The generated header contains matched cubic Bézier endpoints, so the application
does not need JavaScript, SVG animation support, or a downloaded asset at runtime.
Only the mask rotates; the underlying photo or video stays in its original orientation.
Animation follows the application's animation/power-saving setting.

Geometry comes from the Android Open Source Project through
[material-shapes-ts](https://github.com/ruanspies/material-shapes-ts), version
`0.3.0`, commit `9ed10551be53e737110b9b6a10f6231345e7a9c4`.
The npm tarball SHA-512 integrity is
`sha512-YVmrPL/TP9AtXtcQabGccSi0lrNBTCCAOpRySwEhkJYuxr6Hyh4ejIWx3xKxTDS4CrsdItYCS1OAapk5HhOTVQ==`.
The full Apache 2.0 license and attribution are included in the repository's
`LEGAL`, which is also shipped with the Windows installer.

To regenerate, download and verify that version's npm package, extract it outside
the repository, then run from the repository root:

```powershell
node tools/avatar-masks/generate.mjs 'C:\path\to\material-shapes-ts\package'
```

Shapes and timings follow the official AndroidX
[LoadingIndicator](https://github.com/androidx/androidx/blob/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/LoadingIndicator.kt).
The spring is approximated analytically and settles before each 650 ms transition.
