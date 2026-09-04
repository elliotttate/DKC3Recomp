# macOS icon

`DKC3RecompIcon.png` is the 1024-by-1024 source master for
`DKC3Recomp.icns`. It is original project artwork generated with OpenAI's
built-in image-generation tool on 2026-09-03; no game screenshot, character
art, publisher logo, or other source image was used.

The prompt requested a production macOS icon with a polished brass `3` on a
dark-walnut barrel medallion, a deep-teal rounded-square tile, turquoise river,
tropical leaves, transparent outer corners, modern dimensional materials, and
restrained 16-bit highlights. It explicitly excluded characters, publisher
marks, game-title lettering, watermarks, and extra numbers.

To regenerate the checked-in `.icns` from the master, create an iconset with
16, 32, 128, 256, and 512 pixel PNGs and their `@2x` counterparts, then run:

```sh
iconutil -c icns DKC3Recomp.iconset -o DKC3Recomp.icns
```

The generated source output was resampled to 1024 square before those standard
macOS representations were produced. The committed `.icns` is the asset CMake
copies into `DKC3Recomp.app/Contents/Resources`.
