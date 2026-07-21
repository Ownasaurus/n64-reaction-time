import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const path = resolve(process.argv[2] || 'build/reaction_time_4p_stats.z64');
const rom = new Uint8Array(await readFile(path));
const fail = (message) => { throw new Error(message); };
const be32 = (offset) =>
  ((rom[offset] * 0x1000000) + (rom[offset + 1] << 16) +
   (rom[offset + 2] << 8) + rom[offset + 3]) >>> 0;
const hex = (value) => `0x${value.toString(16).padStart(8, '0')}`;
const rol32 = (value, shift) => shift
  ? ((value << shift) | (value >>> (32 - shift))) >>> 0
  : value >>> 0;

if (rom.length !== 2 * 1024 * 1024) fail(`expected 2 MiB, got ${rom.length} bytes`);
if (be32(0x00) !== 0x80371240) fail('not a big-endian .z64 image');
if (be32(0x08) !== 0x80000400) fail(`unexpected entrypoint ${hex(be32(0x08))}`);
if (rom[0x3e] !== 0x45) fail(`country byte is not NTSC-U E: ${rom[0x3e]}`);

const expectedBootPrefix = [
  0x3c08a460, 0x24090400, 0xad090000, 0x3c091000, 0x35291000, 0xad090004,
];
for (let i = 0; i < expectedBootPrefix.length; i++) {
  const actual = be32(0x40 + i * 4);
  if (actual !== expectedBootPrefix[i]) {
    fail(`boot word ${i} is ${hex(actual)}, expected ${hex(expectedBootPrefix[i])}`);
  }
}
const loadHi = be32(0x58) & 0xffff;
const loadLo = be32(0x5c) & 0xffff;
const dmaLength = ((((loadHi << 16) | loadLo) >>> 0) + 1) >>> 0;
if (dmaLength === 0 || dmaLength > 0x00100000) fail(`invalid boot DMA length ${dmaLength}`);
if (0x1000 + dmaLength > rom.length) fail('boot DMA reaches past ROM end');
if (be32(0x74) !== 0x3c0b8000 || be32(0x78) !== 0x356b0400 || be32(0x7c) !== 0x01600008) {
  fail('boot stub does not jump to 0x80000400');
}
if (be32(0x1000) !== 0x3c1d8040 || be32(0x1004) !== 0x27bdfff0) {
  fail('flat program does not begin with the expected crt0 stack initialization');
}

let t1 = 0xf8ca4ddc, t2 = t1, t3 = t1, t4 = t1, t5 = t1, t6 = t1;
for (let offset = 0x1000; offset < 0x101000; offset += 4) {
  const d = be32(offset);
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
if (be32(0x10) !== crc1 || be32(0x14) !== crc2) {
  fail(`CRC mismatch: header ${hex(be32(0x10))}/${hex(be32(0x14))}, calculated ${hex(crc1)}/${hex(crc2)}`);
}

const title = new TextDecoder('ascii').decode(rom.slice(0x20, 0x34)).trimEnd();
console.log(`OK: ${path}`);
console.log(`  title:       ${title}`);
console.log(`  size:        ${rom.length} bytes`);
console.log(`  entrypoint:  ${hex(be32(0x08))}`);
console.log(`  flat DMA:    ${dmaLength} bytes from ROM 0x1000 to RDRAM 0x400`);
console.log(`  header CRC:  ${hex(crc1)} / ${hex(crc2)} (CIC-6102 algorithm)`);
