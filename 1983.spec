Name:           1983
Version:        0.2.0
Release:        1%{?dist}
Summary:        Generic MSX and MSX2 emulator

License:        GPL-2.0-only AND BSD-2-Clause
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
SDL3. It emulates the Z80, TMS9918-family and V9938 video, AY/YM audio,
keyboard, joystick and mouse input, cartridges and common mappers, cassettes,
Philips floppy drives, Sunrise IDE, SD Mapper V2, MegaFlashROM SCC+ SD, and
the MSX2 real-time clock. An openMSXnet-compatible host bridge can expose
TCP/IP UNAPI networking through the separately supplied guest TSR.

The redistributable C-BIOS 0.29 MSX1 main and logo ROMs are included as a
ready-to-run default. Proprietary machine, controller, cartridge, and media
images are not included.

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
%doc README.md INSTALL.md TECHNICAL.md DEVELOPMENT.md ROADMAP.md
%doc BOOT_TARGETS.md 1983.conf.example
%{_bindir}/%{name}
%{_datadir}/applications/io.github.salvogendut.Emulator1983.desktop
%{_datadir}/metainfo/io.github.salvogendut.Emulator1983.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.salvogendut.Emulator1983.png
%dir %{_datadir}/%{name}
%{_datadir}/%{name}/1983-models.conf
%dir %{_datadir}/%{name}/ROMS
%{_datadir}/%{name}/ROMS/README.C-BIOS
%{_datadir}/%{name}/ROMS/cbios_logo_msx1.rom
%{_datadir}/%{name}/ROMS/cbios_main_msx1.rom

%changelog
* Thu Jul 30 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.2.0-1
- Ship the first packaged release with a ready-to-run C-BIOS MSX1 default.
- Add RPM, Flatpak, Windows, Linux, and dual-architecture macOS packaging.
- Include MSX1/MSX2 machines, media, cartridge mappers, storage extensions,
  RTC persistence, and the shared 1984/1985-style SDL3 frontend.
