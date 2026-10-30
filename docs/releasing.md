# Releasing

Push a tag named `vMAJOR.MINOR.PATCH` (a suffix such as `-rc1` marks it a
prerelease) and `.github/workflows/release.yml` builds Release packages and
attaches them to a GitHub release with generated notes and a `SHA256SUMS.txt`:

| Package | Contents |
|---|---|
| `frigate-<tag>-windows-x64.zip`, `-windows-arm64.zip` | `frigate.exe`, `f3d.o2r`, `gamecontrollerdb.txt` |
| `frigate-<tag>-macos-universal.zip` | `Frigate.app`: a universal (arm64 and x86_64) `frigate` with its dylibs in `Contents/MacOS/libs/`, `f3d.o2r`, `gamecontrollerdb.txt` and the icon |
| `frigate-<tag>-linux-x86_64.AppImage`, `-linux-aarch64.AppImage` | The Linux build with its libraries bundled |
| `frigate-<tag>-wiiu.zip` | `wiiu/apps/frigate/frigate.wuhb`; extract it to the root of the SD card |

Every package is a Release build, which leaves out the developer launch
arguments, so the executable takes only `--archive-dir`, `--rom` and `--no-extract`
([building.md](building.md#developer-builds-and-release-builds)). None of them
contain game data: the game extracts its archives from the player's own ROM on
first launch.

The macOS and Linux packages are not signed or notarized. A downloaded macOS
build is quarantined by Gatekeeper until `xattr -dr com.apple.quarantine
Frigate.app` is run on it. Launched from Finder, the app keeps its config and
game archives in `~/Library/Application Support/frigate`, so that is where the
ROM goes; set `SHIP_HOME` to use another folder.

The icons all come from `assets/icon/icon.png`. `scripts/make-icons.sh`
regenerates the Windows `.ico`, the Linux PNG and the Wii U icon from it; the
macOS `.icns` is built from it during packaging.

The workflow also runs every Monday and can be run by hand from the Actions tab.
Those runs upload the packages as workflow artifacts (kept for 7 days) and
publish nothing, so a break in the packaging shows up between releases. Pull
requests don't build the packages; `build-validation.yml` still builds every
platform, arm64 included.
