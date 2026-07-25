const fs = require('fs');
const assert = require('assert');

const app = fs.readFileSync('www/index.html', 'utf8');
const firmware = fs.readFileSync('ESP32_Firmware/Cooler_ESP32_API_Server/Cooler_ESP32_API_Server.ino', 'utf8');
const reference = fs.readFileSync('sample/txt/webpage.txt', 'utf8');

function matchOne(text, expression, label) {
  const result = text.match(expression);
  assert(result, `Missing ${label}`);
  return result[1];
}

// Parse the actual inline application script so syntax regressions fail before APK build.
const appScript = matchOne(app, /<script>([\s\S]*)<\/script>/, 'application script');
new Function(appScript);

// Keep the requested visual stylesheet byte-compatible with the supplied reference.
const appCss = matchOne(app, /<style>([\s\S]*?)<\/style>/, 'application CSS').trim();
const referenceCss = matchOne(reference, /<style>([\s\S]*?)<\/style>/, 'reference CSS').trim();
assert(appCss.startsWith(referenceCss), 'Application CSS no longer matches the supplied reference');

// Every endpoint called by Android must exist in the firmware.
const appRoutes = new Set([...app.matchAll(/apiFetch\(['"]([^?'"]+)/g)].map(match => match[1]));
const firmwareRoutes = new Set([...firmware.matchAll(/server\.on\("([^"]+)"/g)].map(match => match[1]));
for (const route of appRoutes) {
  assert(firmwareRoutes.has(route), `Android endpoint has no firmware handler: ${route}`);
}

// Storage must remain on NVS for all user configuration domains.
for (const key of ['scenarios', 'wifi', 'override', 'protection']) {
  assert(firmware.includes(`saveNvsString("${key}"`), `Missing NVS save path for ${key}`);
  assert(firmware.includes(`loadNvsString("${key}"`), `Missing NVS load path for ${key}`);
}
assert(!firmware.includes('writeFileAtomically'), 'User settings unexpectedly depend on LittleFS again');

// App and firmware compatibility versions must stay synchronized.
const firmwareVersion = Number(matchOne(firmware, /doc\["firmwareApiVersion"\]\s*=\s*(\d+)/, 'firmware API version'));
const requiredVersion = Number(matchOne(app, /Number\(data\.firmwareApiVersion\)\s*>=\s*(\d+)/, 'Android required API version'));
assert.strictEqual(requiredVersion, firmwareVersion, 'Android and firmware API versions differ');

// Basic C++ structural smoke test after removing comments and quoted values.
const strippedFirmware = firmware
  .replace(/\/\*[\s\S]*?\*\//g, '')
  .replace(/\/\/[^\n]*/g, '')
  .replace(/"(?:\\.|[^"\\])*"/g, '')
  .replace(/'(?:\\.|[^'\\])*'/g, '');
assert.strictEqual((strippedFirmware.match(/{/g) || []).length, (strippedFirmware.match(/}/g) || []).length, 'Firmware braces are unbalanced');
assert.strictEqual((strippedFirmware.match(/\(/g) || []).length, (strippedFirmware.match(/\)/g) || []).length, 'Firmware parentheses are unbalanced');

console.log(`Validation passed: ${appRoutes.size} API routes, NVS persistence, API v${firmwareVersion}, reference UI, JS and C++ structure.`);
