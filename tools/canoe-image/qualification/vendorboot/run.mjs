import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
const url = new URL('./target/wasm32-unknown-unknown/release/canoe_vendorboot_wasm_qualification.wasm', import.meta.url);
const module = new WebAssembly.Module(readFileSync(url));
assert.deepEqual(WebAssembly.Module.imports(module), []);
const instance = new WebAssembly.Instance(module, {});
for (let i = 0; i < 3; i++) assert.equal(instance.exports.qualify(i), 1);
console.log('WASM vendor_boot CPIO/gzip/LZ4 outputs match all pre-extraction native CLI goldens; zero runtime imports.');
