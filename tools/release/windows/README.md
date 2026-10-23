# Windows releases

The `vinoGram Windows Release` GitHub Actions workflow builds on standard
`windows-2025` runners. It prepares base libraries first, then builds Qt and
the WebRTC/Rust group in parallel, then compiles and packages the client.
Each dependency group has its own cache, saved before the client build.
The first run is a full dependency build; subsequent runs reuse these caches.

`cache-migration.json` permits reusing the previously verified base and engine
caches after the Qt patch correction. Restored base sources refresh the pinned
Windows compatibility patches before saving the new cache. Qt is rebuilt.
The compatibility fingerprint covers the preparation scripts and CI helper;
changing those inputs disables the old-cache fallback automatically.

Qt's preparation recipe builds and installs Debug and RelWithDebInfo. The CI
helper validates the installed libraries without requesting a separate Release
configuration, which Qt's debug-and-release configuration does not generate.

Changes to Windows release tooling or dependency recipes on `codex/initial-mvp`
start a build automatically with installer version `0.1.0`. Manual runs and
`vinogram-v*` tags still select their requested version.

Set repository secrets `TDESKTOP_API_ID` and `TDESKTOP_API_HASH` before running.
Credentials are never embedded in workflow files, cached dependency groups,
or uploaded build directories. The packaged executable uses these credentials.

Run from an authenticated GitHub CLI:

```powershell
gh workflow run vinogram-windows.yml -f version=0.1.0
gh run list --workflow vinogram-windows.yml
gh run watch RUN_ID --exit-status
gh run download RUN_ID --dir downloads
```

The artifact contains `vinoGram-Setup-0.1.0-x64.exe`, a portable ZIP and
SHA-256 checksums. Both packages include Xray, its license, the D3D compiler
module and the client license. They do not contain account data, subscription
URLs, logs, private build configuration or PDB files. Automatic Telegram
updates are disabled for this fork.

To build a version from a tag, push a `vinogram-v0.1.0` tag. A successful
workflow produces a downloadable artifact; it does not publish a GitHub
Release automatically. To publish the reviewed packages:

```powershell
gh release create vinogram-v0.1.0 downloads/* --title "vinoGram 0.1.0" --notes "First public MVP."
```

Keep the matching source commit and GPL license publicly available with the
release. Inno Setup produces an unsigned installer unless code signing is
configured separately.
