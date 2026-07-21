# N64 Reaction Time Test — 4-player statistics build

This revision keeps the RMG-K/GLideN64-compatible RDP rendering and flat ROM
boot path, while extending the reaction test to all four N64 controller ports.

## Behavior

- Polls controller ports 1 through 4 in one standard Joybus/PIF transaction.
- The first non-Start digital button seen from any player completes the trial.
- A premature non-Start press from any player produces `TOO EARLY!`.
- Start on any controller resets all statistics and does not count as a trial.
- Tracks global successful-trial statistics:
  - minimum reaction time
  - maximum reaction time
  - rounded arithmetic average
  - successful run count
- Statistics begin at zero and false starts are not included.
- Controller reads remain approximately 1 kHz inside the ROM. Under RMG-K
  rollback, the synchronized input value may still update once per emulated frame.

## Rendering

- Standard RDP command lists submitted through DPC_START / DPC_END.
- Double-buffered 320x240 RGBA5551 output.
- VI_ORIGIN changes only during vertical blank.
- Compatible with the GLideN64 rendering path used by RMG-K and with ParaLLEl.

## Build

```bash
npm install
node build.mjs
npm run verify
```

Output: `build/reaction_time_4p_stats.z64`

Vibe coded with ChatGPT-5.6-Sol on High intelligence.