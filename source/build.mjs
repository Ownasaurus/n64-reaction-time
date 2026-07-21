import { readFile, mkdir, writeFile } from "node:fs/promises";

const adapterUrl = new URL("./node_modules/romdevtools/src/toolchains/mips-c/mips-c.js", import.meta.url);
const linkerUrl = new URL("./node_modules/romdevtools/src/toolchains/mips-c/lib/n64.ld", import.meta.url);

/* The shared compiler adapter was already extended by the previous build to expose
   the linked ELF and flat objcopy output.  Keep this check so a clean npm install
   can be patched reproducibly. */
let adapter = await readFile(adapterUrl, "utf8");
if (!adapter.includes("rawBinary: oc.binary")) {
  const oldReturn = 'return { ok: true, binary, log: cb.log, exitCode: 0, stage: "done", ...(ld.map ? { symbols: ld.map } : {}) };';
  const newReturn = 'return { ok: true, binary, elf: ld.elf, rawBinary: oc.binary, log: cb.log, exitCode: 0, stage: "done", ...(ld.map ? { symbols: ld.map } : {}) };';
  if (!adapter.includes(oldReturn)) throw new Error("Unsupported romdevtools mips-c adapter version");
  await writeFile(adapterUrl, adapter.replace(oldReturn, newReturn));
}

const linkerScript = `OUTPUT_FORMAT("elf32-bigmips")
OUTPUT_ARCH(mips)
ENTRY(_start)
PHDRS { game PT_LOAD FLAGS(7); }
MEMORY { ram (rwx) : ORIGIN = 0x80000400, LENGTH = 0x3FFC00 }
SECTIONS {
  .text : { *(.text.start) *(.text*) *(.rodata*) } > ram :game
  .data : { *(.data*) *(.sdata*) *(.lit8) *(.lit4) *(.scommon) } > ram :game
  . = ALIGN(8);
  __bss_start = .;
  .bss : { *(.bss*) *(.sbss*) *(.scommon) *(COMMON) } > ram :game
  . = ALIGN(8);
  __bss_end = .;
  __sp_top = 0x803FFFF0;
  /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.comment) }
}`;
await writeFile(linkerUrl, linkerScript);

const { buildMipsC } = await import(`${adapterUrl.href}?rmgk=1`);
const result = await buildMipsC({
  platform: "n64",
  sources: {
    "main.c": await readFile(new URL("./main.c", import.meta.url), "utf8"),
    "n64.c": await readFile(new URL("./n64.c", import.meta.url), "utf8"),
  },
  headers: { "n64.h": await readFile(new URL("./n64.h", import.meta.url), "utf8") },
});
if (!result.ok || !result.rawBinary || !result.elf) {
  console.error(result.log || result);
  process.exit(result.exitCode || 1);
}

const raw = Uint8Array.from(result.rawBinary);
const elf = Uint8Array.from(result.elf);
const paddedLoadSize = (raw.length + 7) & ~7;

const be32 = (array, offset, value) => {
  array[offset] = (value >>> 24) & 255;
  array[offset + 1] = (value >>> 16) & 255;
  array[offset + 2] = (value >>> 8) & 255;
  array[offset + 3] = value & 255;
};
const readBe32 = (array, offset) =>
  ((array[offset] * 0x1000000) + (array[offset + 1] << 16) +
   (array[offset + 2] << 8) + array[offset + 3]) >>> 0;
const rol32 = (value, shift) => shift ? ((value << shift) | (value >>> (32 - shift))) >>> 0 : value >>> 0;

/* Clean-room HLE-oriented IPL3.  Unlike the earlier wrapper, the DMA length is
   the exact application size rather than a blanket 1 MiB. */
const words = [
  0x3C08A460,                    // lui   t0, 0xA460 (PI registers)
  0x24090400,                    // addiu t1, zero, 0x0400 (RDRAM physical)
  0xAD090000,                    // sw    t1, PI_DRAM_ADDR
  0x3C091000,                    // lui   t1, 0x1000
  0x35291000,                    // ori   t1, t1, 0x1000 (cart program offset)
  0xAD090004,                    // sw    t1, PI_CART_ADDR
  (0x3C090000 | (((paddedLoadSize - 1) >>> 16) & 0xFFFF)) >>> 0,
  (0x35290000 | ((paddedLoadSize - 1) & 0xFFFF)) >>> 0,
  0xAD09000C,                    // sw    t1, PI_WR_LEN (cart -> RDRAM)
  0x8D0A0010,                    // wait for PI DMA/IO idle
  0x314A0003,
  0x1540FFFD,
  0x00000000,
  0x3C0B8000,
  0x356B0400,
  0x01600008,                    // jr    0x80000400
  0x00000000,
];

/* 2 MiB avoids short-ROM edge cases in older PI implementations and provides
   the full 1 MiB checksum window beginning at 0x1000. */
const rom = new Uint8Array(2 * 1024 * 1024);
be32(rom, 0x00, 0x80371240);
be32(rom, 0x04, 0x0000000F);
be32(rom, 0x08, 0x80000400);
be32(rom, 0x0C, 0x00001444);
be32(rom, 0x10, 0);
be32(rom, 0x14, 0);
const title = "REACTION 4P STATS ".padEnd(20, " ");
for (let i = 0; i < 20; i++) rom[0x20 + i] = title.charCodeAt(i);
be32(rom, 0x38, 0x0000004E);
rom[0x3C] = 0x52; rom[0x3D] = 0x54; rom[0x3E] = 0x45; rom[0x3F] = 0;
for (let i = 0; i < words.length; i++) be32(rom, 0x40 + i * 4, words[i]);
rom.set(raw, 0x1000);

/* Standard CIC-6102 cartridge CRCs.  RMG-K's Mupen core falls back to 6102 for
   an unknown clean-room IPL3 checksum, so these keep the header internally sane. */
let t1 = 0xF8CA4DDC, t2 = t1, t3 = t1, t4 = t1, t5 = t1, t6 = t1;
for (let offset = 0x1000; offset < 0x101000; offset += 4) {
  const d = readBe32(rom, offset);
  const sum = (t6 + d) >>> 0;
  if (sum < t6) t4 = (t4 + 1) >>> 0;
  t6 = sum;
  t3 = (t3 ^ d) >>> 0;
  const r = rol32(d, d & 31);
  t5 = (t5 + r) >>> 0;
  t2 = (t2 > d ? (t2 ^ r) : (t2 ^ t6 ^ d)) >>> 0;
  t1 = (t1 + (t5 ^ d)) >>> 0;
}
const crc1 = ((t6 ^ t4) + t3) >>> 0;
const crc2 = ((t5 ^ t2) + t1) >>> 0;
be32(rom, 0x10, crc1);
be32(rom, 0x14, crc2);
be32(rom, 0x18, 0);
be32(rom, 0x1C, 0);

await mkdir(new URL("./build/", import.meta.url), { recursive: true });
await writeFile(new URL("./build/reaction_time_4p_stats.z64", import.meta.url), rom);
await writeFile(new URL("./build/reaction_time.bin", import.meta.url), raw);
await writeFile(new URL("./build/reaction_time.elf", import.meta.url), elf);
await writeFile(new URL("./build/build.log", import.meta.url), result.log || "Build succeeded.\n");
console.log(`Built reaction_time_4p_stats.z64: ROM=${rom.length}, program=${raw.length}, DMA=${paddedLoadSize}, CRC=${crc1.toString(16).padStart(8,"0")}/${crc2.toString(16).padStart(8,"0")}`);
