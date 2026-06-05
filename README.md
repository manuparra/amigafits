# AmigaFITS

AmigaFITS is a minimum viable FITS image viewer for Amiga Workbench 1.3.

The first target is the included `img.fits` astronomy image. It is a
157x157 primary FITS image with 32-bit floating point pixels.

## MVP behavior

- Runs on AmigaOS/Workbench 1.3.
- Opens `img.fits` by default or a path passed as `amigafits <file>`.
- Accepts an optional display depth from 1 to 4 bitplanes; lower depth uses
  fewer colors and renders faster.
- Supports primary FITS images with `BITPIX=-32`, `NAXIS=2`, `NAXIS1`, and
  `NAXIS2`.
- Scales images to cover the 320x200 low-resolution display area while
  preserving aspect ratio, cropping centered overflow when needed.
- Opens a 320x200 custom screen with the selected depth and a false-color
  colormap.
- Maps FITS pixel values to the palette with a temporary fixed `190..650` data
  range, saturating values outside that range.
- Keeps the image visible until a key press, mouse button, or close event.

## Build

The build follows the same model as `manuparra/a68k-shell`: vbcc inside Docker
with Kickstart 1.3 settings.

```sh
make build
```

The output binary is written to:

```text
build/amigafits
```

The Docker image is:

```text
vintagecomputingcarinthia/vbcc4vcc:latest
```

The build script compiles with:

```text
vc +kick13
```

## Run in FS-UAE

Set the Kickstart 1.3 ROM path:

```sh
export FSUAE_KICKSTART_FILE=/path/to/kickstart-1.3.rom
```

Optionally set a Workbench 1.3 ADF:

```sh
export FSUAE_WORKBENCH_ADF=/path/to/Workbench1.3.adf
```

Then run:

```sh
make run-fsuae
```

The script builds `build/amigafits` if needed, copies `amigafits` and
`img.fits` to `dist/AmigaFITS/`, generates `build/amigafits.fs-uae`, and
mounts `dist/AmigaFITS` as a hard drive directory.

To generate the FS-UAE config without launching the emulator:

```sh
FSUAE_NO_LAUNCH=1 make run-fsuae
```

## Usage

From Amiga CLI:

```text
amigafits
amigafits img.fits
amigafits img.fits 2
```

The optional depth argument must be between `1` and `4`. Depth `1` uses
2 colors, depth `2` uses 4 colors, depth `3` uses 8 colors, and depth `4`
uses 16 colors. The default is `4`.

The CLI prints simple progress bars while loading, scaling, and converting the
image so the command does not appear stalled before the graphics screen opens.

From Workbench 1.3, open the mounted `AmigaFITS` drawer and run `amigafits`.
If no icon is available in this MVP, enable `Show All Files` and double-click
the executable. The default image path is `img.fits`, so keep it beside the
binary unless you pass another path from CLI.

## Acceptance test

With the bundled `img.fits`:

- `make build` creates `build/amigafits`.
- `FSUAE_NO_LAUNCH=1 make run-fsuae` creates `build/amigafits.fs-uae` and
  copies both the binary and `img.fits` into `dist/AmigaFITS/`.
- In Workbench 1.3 or Amiga CLI, running `amigafits` opens a graphics screen
  and shows the image scaled to cover the screen while preserving aspect
  ratio. Overflow is cropped from the center.
- Pressing any key or mouse button exits the viewer.

## Current limitations

- Only primary 2D FITS images with `BITPIX=-32` are supported.
- No zooming, panning, color palette selection, or histogram UI yet.
- Contrast currently assumes a fixed `190..650` input range.
