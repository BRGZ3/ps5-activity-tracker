const fs = require("fs");

if (process.argv.length !== 9) {
  console.error("usage: node build-carrier.js <png> <output> <dashboard> <elf> <package-version> <tracker-version> <dashboard-version>");
  process.exit(2);
}

const png = fs.readFileSync(process.argv[2]);
const output = process.argv[3];
const entries = [
  { type: 1, data: fs.readFileSync(process.argv[4]) },
  { type: 3, data: fs.readFileSync(process.argv[5]) },
];

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++)
      crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function fixed(value, length) {
  const buffer = Buffer.alloc(length);
  buffer.write(String(value).slice(0, length - 1), 0, "ascii");
  return buffer;
}

const header = Buffer.concat([
  Buffer.from("PLGBND02", "ascii"),
  Buffer.from([2, 0, 0, 0]),
  Buffer.from([entries.length, 0, 0, 0]),
  fixed(process.argv[6], 16),
  fixed(process.argv[7], 16),
  fixed(process.argv[8], 16),
]);
const chunks = [header];
for (const entry of entries) {
  const meta = Buffer.alloc(16);
  meta.writeUInt32LE(entry.type, 0);
  meta.writeUInt32LE(entry.data.length, 4);
  meta.writeUInt32LE(crc32(entry.data), 8);
  chunks.push(meta, entry.data);
}
const bundle = Buffer.concat(chunks);
const footer = Buffer.alloc(56);
footer.write("PLGUPD02", 0, 8, "ascii");
footer.writeBigUInt64LE(BigInt(png.length), 8);
footer.writeBigUInt64LE(BigInt(bundle.length), 16);
footer.writeUInt32LE(crc32(bundle), 24);
fs.writeFileSync(output, Buffer.concat([png, bundle, footer]));
console.log(`Playlog carrier: png=${png.length}, bundle=${bundle.length}, total=${png.length + bundle.length + footer.length}`);
