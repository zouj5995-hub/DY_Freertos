import assert from 'node:assert/strict';

function crc16(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = crc & 1 ? (crc >>> 1) ^ 0xa001 : crc >>> 1;
  }
  return crc & 0xffff;
}

function frame(head, boat, data = []) {
  const body = Uint8Array.from([...Buffer.from(head), boat, ...data]);
  const crc = crc16(body);
  return Uint8Array.from([...body, crc >>> 8, crc & 0xff, ...Buffer.from('$OVER')]);
}

const hex = bytes => [...bytes].map(byte => byte.toString(16).padStart(2, '0').toUpperCase()).join(' ');
assert.equal(hex(frame('$READ', 1)), '24 52 45 41 44 01 EB 08 24 4F 56 45 52');
assert.equal(hex(frame('$GETSTR', 1)), '24 47 45 54 53 54 52 01 89 E7 24 4F 56 45 52');
assert.equal(hex(frame('$REST', 1)), '24 52 45 53 54 01 2E A5 24 4F 56 45 52');
assert.equal(frame('$STR', 1, new Uint8Array(210)).length, 222);
console.log('Protocol vectors OK');
