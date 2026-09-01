Name:           1983
Version:        0.4.0
Release:        1%{?dist}
Summary:        Generic MSX and MSX2 emulator

License:        GPL-2.0-only AND BSD-2-Clause AND BSD-3-Clause AND Zlib AND CC0-1.0
URL:            https://github.com/salvogendut/1983
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  pkgconfig(sdl3)
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

%description
1983 is a compatibility-focused MSX and MSX2 emulator written in C with
SDL3. It emulates the Z80, TMS9918-family, V9938/V9958, and PowerGraph V9990
video, AY/YM audio, keyboard, joystick and mouse input, cartridges and common
mappers, cassettes, Philips floppy drives, Sunrise IDE, SD Mapper V2,
MegaFlashROM SCC+ SD, NCR/Z5380 MSX SCSI, and the MSX2 real-time clock. An
openMSXnet-compatible host bridge can expose TCP/IP UNAPI networking through
the separately supplied guest TSR.

The redistributable RainBIOS Omega MSX2 firmware is included as the
ready-to-run default. C-BIOS 0.29 remains available as a bundled MSX1 model.
Proprietary machine, controller, cartridge, and media images are not included.

%prep
%autosetup

%build
autoreconf -fiv
%configure
%make_build

%install
%make_install

%check
%make_build check
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.salvogendut.Emulator1983.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/io.github.salvogendut.Emulator1983.metainfo.xml

%files
%license LICENSE
%doc README.md INSTALL.md TECHNICAL.md DEVELOPMENT.md ROADMAP.md SCSI.md
%doc BOOT_TARGETS.md 1983.conf.example
%{_bindir}/%{name}
%{_datadir}/applications/io.github.salvogendut.Emulator1983.desktop
%{_datadir}/metainfo/io.github.salvogendut.Emulator1983.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.salvogendut.Emulator1983.png
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/1983-models.conf
%dir %{_datadir}/%{name}/ROMS
%{_datadir}/%{name}/ROMS/README.C-BIOS
%{_datadir}/%{name}/ROMS/README-RainBIOS
%{_datadir}/%{name}/ROMS/README-MSXSCSI
%{_datadir}/%{name}/ROMS/BertSCSI-v1-D0h-D7h.ROM
%{_datadir}/%{name}/ROMS/BertSCSI-v2-30h-37h.ROM
%{_datadir}/%{name}/ROMS/cbios_logo_msx1.rom
%{_datadir}/%{name}/ROMS/cbios_main_msx1.rom
%{_datadir}/%{name}/ROMS/rainbios_msx2.rom
%{_datadir}/%{name}/ROMS/rainbios_msx2_sub.rom
%{_datadir}/%{name}/ROMS/rainbios_disk.rom
%{_datadir}/%{name}/ROMS/rainbios_omega.rom

%changelog
* Sat Aug 29 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.0-1
- Add PowerGraph/GFX9000 V9990 emulation to native and WebAssembly builds.
- Add automatic VDP/V9990 output switching and SYMG9K/SymbOS integration.
- Emulate V9990 bitmap modes, hardware cursors, and CPU command transfers.
- Correct RS-232 8254 timer gate state after device creation.

* Fri Aug 28 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.3.0-1
- Add the WebAssembly browser edition and URL-configurable startup media.
- Ship RainBIOS Omega MSX2 as the default with unified-ROM upload support.
- Add V9958 selection, more floppy controllers, and SymbOS storage boot.
- Add TCP/IP UNAPI networking, relay integration, and expanded media controls.

* Thu Jul 30 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.2.0-1
- Ship the first packaged release with a ready-to-run C-BIOS MSX1 default.
- Add RPM, Flatpak, Windows, Linux, and dual-architecture macOS packaging.
- Include MSX1/MSX2 machines, media, cartridge mappers, storage extensions,
  RTC persistence, and the shared 1984/1985-style SDL3 frontend.
