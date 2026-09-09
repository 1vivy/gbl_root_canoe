import { readFileSync, existsSync } from 'node:fs';
import assert from 'node:assert/strict';
const root = new URL('../../../../', import.meta.url);
const goldens = JSON.parse(readFileSync(new URL('submodules/ablfvextractor/tests/goldens.json', root)));
const wasm = new WebAssembly.Module(readFileSync(new URL('./target/wasm32-unknown-unknown/release/canoe_loader_wasm_qualification.wasm', import.meta.url)));
assert.deepEqual(WebAssembly.Module.imports(wasm), []);
const {exports: api} = new WebAssembly.Instance(wasm, {});
assert.equal(api.qualify_config(), 1, 'canonical config/BLS JSON/wire roundtrip');
for (const [index, fixture] of goldens.entries()) {
  const path = new URL(fixture.source, root);
  if (fixture.optional_external_fixture && !existsSync(path)) { console.log(`SKIP optional external ${fixture.source}`); continue; }
  const input = readFileSync(path);
  const pointer = api.allocate(input.length);
  assert.ok(pointer);
  new Uint8Array(api.memory.buffer, pointer, input.length).set(input);
  assert.equal(api.qualify(pointer, input.length, index), 1, fixture.source);
  console.log(`PASS ${fixture.source}: extraction + patched loader + GM2P + TZ map`);
}
console.log(`WASM actual runtime matches ${goldens.length} native CLI goldens; canonical config/BLS pass; zero runtime imports; memory ${api.memory.buffer.byteLength} bytes.`);
