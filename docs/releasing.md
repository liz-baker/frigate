# Releasing

Push a tag named `vMAJOR.MINOR.PATCH` (a suffix such as `-rc1` marks it a
prerelease) and `.github/workflows/release.yml` builds Release packages and
attaches them to a GitHub release with generated notes and a `SHA256SUMS.txt`:

| Package | Contents |
|---|---|
| `frigate-<tag>-windows-x64.zip`, `-windows-arm64.zip` | `frigate.exe`, `f3d.o2r`, `gamecontrollerdb.txt` |
| `frigate-<tag>-macos-universal.zip` | A universal (arm64 and x86_64) `frigate` with its dylibs in `libs/`, `f3d.o2r`, `gamecontrollerdb.txt` |
| `frigate-<tag>-linux-x86_64.AppImage`, `-linux-aarch64.AppImage` | The Linux build with its libraries bundled |
| `frigate-<tag>-wiiu.zip` | `wiiu/apps/frigate/frigate.wuhb`; extract it to the root of the SD card |

Every package is a Release build, which leaves out the developer launch
arguments, so the executable takes only `--archive-dir`, `--rom` and `--no-extract`
([building.md](building.md#developer-builds-and-release-builds)). None of them
contain game data: the game extracts its archives from the player's own ROM on
first launch.

The macOS and Linux packages are not signed or notarized. A downloaded macOS
build is quarantined by Gatekeeper until `xattr -dr com.apple.quarantine
<folder>` is run on it.

To try the packages without tagging, run the workflow by hand from the Actions
tab: it uploads them as workflow artifacts (kept for 7 days) and publishes nothing. A pull
request that changes the workflow runs it the same way, keeping the packages for a day.
