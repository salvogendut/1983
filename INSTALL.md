# Installing 1983

## Release packages

Tagged releases are published on the
[GitHub Releases page](https://github.com/salvogendut/1983/releases).

| Asset | Platform | Use |
|---|---|---|
| `1983-vX.Y.Z-linux-x86_64.tar.gz` | Linux x86_64 | Extract and run `./1983`; SDL3 is required |
| `1983-X.Y.Z-1.*.x86_64.rpm` | Fedora, RHEL-family | Install with the system RPM package manager |
| `1983-vX.Y.Z-windows-x86_64.zip` | Windows x86_64 | Extract and run `1983.exe`; required DLLs are included |
| `1983-vX.Y.Z-macos-arm64.zip` | Apple Silicon macOS | Extract and open `1983.app` |
| `1983-vX.Y.Z-macos-x86_64.zip` | Intel macOS | Extract and open `1983.app` |
| `1983-vX.Y.Z-x86_64.flatpak` | Flatpak x86_64 | Install with `flatpak install --user FILE` |

Every bundle includes `1983-models.conf`, the redistributable 512 KiB
RainBIOS Omega unified ROM and its component images, and the C-BIOS MSX1
firmware. A fresh launch therefore boots the `omega-msx2` model with a V9958,
128 KiB RAM, and its Philips-compatible floppy controller without downloading
proprietary firmware. The `cbios` model remains available for MSX1 software.

The TCP/IP UNAPI host bridge is included on every platform. Its separate
guest driver, openMSXnet v0.9.7's `UNAPINET.COM`, is not bundled; download it
from the upstream release and copy it to a Nextor/MSX-DOS 2 disk as described
in [`BOOT_TARGETS.md`](BOOT_TARGETS.md). The Flatpak manifest grants network
access so the bridge can reach the host network stack.

The macOS bundles are ad-hoc signed, not notarized with an Apple Developer
ID. The first launch may require right-clicking the application and choosing
**Open**.

## Build and install from source

Requirements are a C11 compiler, GNU Autotools, `pkg-config`, GNU Make, SDL3
development files, and `libm`.

```sh
autoreconf -iv
./configure
make -j4
make check
sudo make install
```

The default prefix is `/usr/local`. Pass `--prefix=/usr` when preparing a
system package.

On Fedora:

```sh
sudo dnf install gcc make autoconf automake pkgconf-pkg-config SDL3-devel
autoreconf -iv
./configure
make -j"$(nproc)"
make dist
rpmbuild -ta 1983-0.2.0.tar.gz
```

On Windows, run the normal Autotools sequence from an MSYS2 MinGW64 shell
after installing `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-pkgconf`,
`mingw-w64-x86_64-sdl3`, `autoconf`, `automake`, and `make`.

On macOS, install `autoconf`, `automake`, `pkg-config`, and `sdl3` with
Homebrew, then use the normal source-build commands above.

## Build the Flatpak locally

The manifest uses the Freedesktop 25.08 runtime and packages the current
checkout, including uncommitted source changes.

```sh
flatpak install --user flathub \
  org.freedesktop.Platform//25.08 org.freedesktop.Sdk//25.08
flatpak-builder --user --install --force-clean flatpak_app \
  io.github.salvogendut.Emulator1983.yml
flatpak run io.github.salvogendut.Emulator1983
```

To create a single-file bundle:

```sh
flatpak-builder --force-clean --repo=flatpak-repo flatpak_app \
  io.github.salvogendut.Emulator1983.yml
flatpak build-bundle flatpak-repo 1983.flatpak \
  io.github.salvogendut.Emulator1983
```
