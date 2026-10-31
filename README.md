# dragons-lair-trilogy-wii-fix

Restores the **red "ENERGIZE" sequences** in *Space Ace* on the Wii release of **Don Bluth Presents Dragon's Lair Trilogy** (USA, `SL3ENR`).

The Wii version re-graded every energize sequence blue (an accessibility change), which loses the look of the original laserdisc game. `dltfix` takes **your own disc image** and writes a patched copy with the energize sequences back in red. You can play the result in Dolphin, load it from a USB loader, or burn it for a modded Wii.

**No game content is included.** Every restored frame is generated on your machine from your own disc.

## Usage

1. Dump your disc to a plain `.iso`. If you have RVZ/WBFS/NKit, convert it to ISO first (Dolphin: right-click the game > *Convert File* > ISO).
2. Drag the `.iso` onto `dltfix.exe`, or run:

```
dltfix.exe "Dragon's Lair Trilogy.iso"
```

The patched image is written next to the original as `..._red_energize.iso`. You need about 5 GB of free disk space. A run takes a few minutes; if it's interrupted, run it again and it continues where it stopped.

Options: `[output.iso]`, `--threads N`, `--work DIR`, `--keep-work`.

## What it does

1. **Checks** that the input is `SL3ENR` and that the 33 video files it uses are untouched retail data (SHA-1).
2. **Decodes** the 32 blue energize segments (both 4:3 and widescreen versions) with a MobiClip decoder ported from FFmpeg.
3. **Restores the color.** It uses a smooth color model fitted to the one energize scene whose red original still exists, unused, on the same disc (`SAScene1_original.mo`). It then applies edge-preserving denoising. For one sequence (the tube scene, `SAScene4/5`), colors that can't occur under the red tint are pulled into the red/pink family.
4. **Re-encodes** each segment with a purpose-built Wii MobiClip encoder. It keeps every keyframe position, the per-keyframe signature words the Wii player checks, and every audio byte untouched. `SAScene1` is replaced outright by the developers' red original.
5. **Rebuilds the disc image.** Files keep their original order and position, the partition is rehashed (H0-H3), and the TMD is fakesigned.
6. **Verifies** the new image: patched files read back exactly, all other files are byte-identical to your original, and every hash in the partition checks out. It also reports whether the output matches the reference build.

The tool is deterministic: the same input disc gives a byte-identical result on any machine.

## Building

```
make dltfix        # Linux / macOS (gcc or clang)
make dltfix.exe    # Windows, cross-compiled with MinGW-w64
```

Plain C11 with pthreads and no other dependencies. CI builds both on every push; tagged versions are attached to a GitHub release.

## Notes

- The patched image is fakesigned. That's fine for Dolphin and USB loaders. A burned disc needs a modded or softmodded Wii, like any backup.
- `SAScene1_original.mo` exists on the retail disc but the game never loads it. It is the pre-release red version of the first energize scene, and it is the ground truth the color model was fitted to.

## License

LGPL-2.1-or-later (see `LICENSE`). The MobiClip decoder (`src/mobidec.c`, `src/mobitab.h`) is a port of FFmpeg's `libavcodec/mobiclip.c` by Florian Nouwt, Adib Surani and Paul B Mahol. The encoder mirrors its reconstruction logic.
