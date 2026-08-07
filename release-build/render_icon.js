#!/usr/bin/env node
const fs = require("fs");
const zlib = require("zlib");

const size = 512;
const pixels = Buffer.alloc(size * size * 4);
const colors = {
  bg: [10, 11, 12, 255],
  panel: [24, 26, 27, 255],
  ink: [244, 241, 232, 255],
  signal: [217, 255, 67, 255],
};
if (process.argv[3] === "test") {
  colors.bg = [8, 13, 35, 255];
  colors.panel = [26, 23, 55, 255];
  colors.ink = [248, 244, 255, 255];
  colors.signal = [255, 78, 132, 255];
}
if (process.argv[3] === "bootstrap") {
  colors.bg = [20, 10, 4, 255];
  colors.panel = [43, 24, 12, 255];
  colors.ink = [255, 248, 231, 255];
  colors.signal = [255, 151, 45, 255];
}
if (process.argv[3] === "clean") {
  colors.bg = [3, 21, 24, 255];
  colors.panel = [8, 40, 44, 255];
  colors.ink = [238, 255, 251, 255];
  colors.signal = [36, 224, 184, 255];
}

function setPixel(x, y, color) {
  if (x < 0 || y < 0 || x >= size || y >= size) return;
  const offset = (y * size + x) * 4;
  pixels.set(color, offset);
}
function roundedRect(x, y, w, h, radius, color) {
  for (let py = y; py < y + h; py++) {
    for (let px = x; px < x + w; px++) {
      const cx = Math.max(x + radius, Math.min(px, x + w - radius - 1));
      const cy = Math.max(y + radius, Math.min(py, y + h - radius - 1));
      if ((px - cx) ** 2 + (py - cy) ** 2 <= radius ** 2) {
        setPixel(px, py, color);
      }
    }
  }
}
function rect(x, y, w, h, color) {
  for (let py = y; py < y + h; py++)
    for (let px = x; px < x + w; px++) setPixel(px, py, color);
}
function circle(cx, cy, radius, color) {
  for (let y = cy - radius; y <= cy + radius; y++)
    for (let x = cx - radius; x <= cx + radius; x++)
      if ((x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2)
        setPixel(x, y, color);
}

rect(0, 0, size, size, colors.bg);
roundedRect(48, 48, 416, 416, 48, colors.signal);
roundedRect(57, 57, 398, 398, 40, colors.panel);
rect(150, 136, 70, 242, colors.ink);
roundedRect(185, 136, 196, 194, 72, colors.ink);
roundedRect(220, 198, 92, 70, 24, colors.panel);
circle(383, 383, 28, colors.signal);

const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(buffer) {
  let c = 0xffffffff;
  for (const byte of buffer) c = crcTable[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const name = Buffer.from(type);
  const output = Buffer.alloc(12 + data.length);
  output.writeUInt32BE(data.length, 0);
  name.copy(output, 4);
  data.copy(output, 8);
  output.writeUInt32BE(crc32(Buffer.concat([name, data])), 8 + data.length);
  return output;
}

const scanlines = Buffer.alloc((size * 4 + 1) * size);
for (let y = 0; y < size; y++) {
  const row = y * (size * 4 + 1);
  scanlines[row] = 0;
  pixels.copy(scanlines, row + 1, y * size * 4, (y + 1) * size * 4);
}
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(size, 0);
ihdr.writeUInt32BE(size, 4);
ihdr.set([8, 6, 0, 0, 0], 8);
const png = Buffer.concat([
  Buffer.from("89504e470d0a1a0a", "hex"),
  chunk("IHDR", ihdr),
  chunk("IDAT", zlib.deflateSync(scanlines, { level: 9 })),
  chunk("IEND", Buffer.alloc(0)),
]);
fs.writeFileSync(process.argv[2] || "ACTV00002/icon0.png", png);
