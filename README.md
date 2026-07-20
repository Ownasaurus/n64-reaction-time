# N64 Reaction Time Test — RDP / GLideN64 build

This version keeps the RMG-K-compatible flat ROM boot path and safe Joybus
controller handling from the working ParaLLEl build, but replaces all direct
CPU framebuffer drawing with normal RDP command lists.

## Rendering changes

* Uses DPC\_START / DPC\_END to submit raw RDP command lists.
* Uses SetColorImage, SetOtherModes (fill cycle), SetScissor, SetFillColor,
FillRectangle, PipeSync, and FullSync.
* Draws the panel, circles, text, and background through RDP fill rectangles.
* Double-buffers at 320x240 RGBA5551.
* Waits for vertical blank before changing VI\_ORIGIN.
* Clears the DP interrupt after polling FullSync completion.
* Does not depend on GLideN64's CPU-framebuffer-write detection.

## Build

```bash
npm install
node build.mjs
node verify_rom.mjs
node verify_rdp_commands.mjs
```

Output: `build/reaction_time_gliden64.z64`

The ROM is intended for RMG-K with GLideN64 and should remain compatible with
ParaLLEl. Runtime behavior still needs validation in the user's Windows RMG-K
build because that exact GUI/plugin environment is not available here.

Vibe coded with ChatGPT-5.6-Sol on High intelligence.

