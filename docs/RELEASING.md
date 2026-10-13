# Releasing AirMirror

`VERSION` controls both the application version and MSI version. A release uses a `vMAJOR.MINOR.PATCH` annotated tag and four assets: the x64 MSI, portable ZIP, full source ZIP, and `SHA256SUMS.txt`.

Review the changes, stage only intended source files, then run:

```powershell
.\scripts\Commit-Release.ps1 -Message "Describe the change" -SkipBootstrap
```

The script bumps the patch version, commits staged changes plus `VERSION`, builds and validates the MSI locally, tags and pushes the commit, then publishes a GitHub release with the commit message as its notes. Use `-Bump Minor` or `-Bump Major` when appropriate. Omit `-SkipBootstrap` to refresh MSYS2 packages. If the build fails, the commit remains locally but no tag is pushed; fix and rebuild before publishing. Never publish an installer that failed bundle tests or WiX validation.

Every push to `main` also runs the Windows Actions build and uploads the MSI and portable ZIP as workflow artifacts. The workflow is an independent build check; the local release script publishes the versioned public assets used by the updater. GitHub CLI authentication and push/release access to `heltonfonias/AirMirror-by-Fonias` are required.

AirMirror's updater asks the GitHub latest-stable-release API for the x64 MSI. GitHub must expose a SHA-256 digest for that asset. Its download is verified against the digest before Windows Installer is launched with UAC. Do not mark incomplete builds as a stable release.
