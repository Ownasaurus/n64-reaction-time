const cmd = w0 => (w0 >>> 24) & 0x3f;
const sh = (v, n, bits) => (v >>> n) & ((1 << bits) - 1);
const SCREEN_W = 320, SCREEN_H = 240;

const setColorW0 = (0xFF000000 | (2 << 19) | (SCREEN_W - 1)) >>> 0;
if (cmd(setColorW0) !== 0x3f) throw new Error('SetColorImage opcode mismatch');
if (sh(setColorW0, 21, 3) !== 0) throw new Error('Color format is not RGBA');
if (sh(setColorW0, 19, 2) !== 2) throw new Error('Color size is not 16-bit');
if (sh(setColorW0, 0, 12) + 1 !== SCREEN_W) throw new Error('Width mismatch');

const otherW0 = (0xEF000000 | 0x00300000) >>> 0;
if (cmd(otherW0) !== 0x2f) throw new Error('SetOtherModes opcode mismatch');
if (sh(otherW0, 20, 2) !== 3) throw new Error('Cycle type is not fill');

const scissorW0 = 0xED000000 >>> 0;
const scissorW1 = (((SCREEN_W * 4) << 12) | (SCREEN_H * 4)) >>> 0;
if (cmd(scissorW0) !== 0x2d) throw new Error('SetScissor opcode mismatch');
if (sh(scissorW1, 12, 12) !== SCREEN_W * 4) throw new Error('Scissor width mismatch');
if (sh(scissorW1, 0, 12) !== SCREEN_H * 4) throw new Error('Scissor height mismatch');

const x0=10, y0=10, x1=310, y1=230;
const fillW0 = (0xF6000000 | ((x1-1)<<14) | ((y1-1)<<2)) >>> 0;
const fillW1 = ((x0<<14) | (y0<<2)) >>> 0;
if (cmd(fillW0) !== 0x36) throw new Error('FillRectangle opcode mismatch');
if (sh(fillW1,14,10) !== x0 || sh(fillW1,2,10) !== y0) throw new Error('Fill UL mismatch');
if (sh(fillW0,14,10) !== x1-1 || sh(fillW0,2,10) !== y1-1) throw new Error('Fill LR mismatch');

console.log('RDP command encodings match GLideN64 RDP.cpp decoding.');
