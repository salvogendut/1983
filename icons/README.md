# 1983 icon assets

`1983.png` is the transparent 1024x1024 master derived from the project
artwork supplied by the maintainer. The artwork and the `1983` lettering are
kept unchanged; the source was centred on a square transparent canvas before
resizing.

The hicolor assets use the application ID
`io.github.salvogendut.Emulator1983` at 16, 32, 48, 64, 128, 256, and 512
pixels. `1983.ico` contains the 16 through 256 pixel variants for the Windows
executable resource declared by `1983.rc`.

The PNG sizes are Lanczos resamples of the master with a light unsharp pass.
Regenerate a size with ImageMagick using:

```sh
magick icons/1983.png -filter Lanczos -resize SIZExSIZE \
  -unsharp 0x0.5+0.35+0.02 \
  icons/SIZExSIZE/apps/io.github.salvogendut.Emulator1983.png
```
