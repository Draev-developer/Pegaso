// Drives the browser extension inside a real browser, through the DevTools protocol.
//   node exttest.mjs <debug port> <api port> <key> <json array of links> [kind]
// Finds the background script of the extension, gives it the key, and has it hand the links
// over to Pegaso with its own code. Prints one JSON line with what happened.
const [debugPort, apiPort, key, linksJson, kind] = process.argv.slice(2);

function connect(target) {
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  return new Promise((resolve, reject) => {
    ws.addEventListener('open', () => resolve(ws));
    ws.addEventListener('error', reject);
  });
}

function evaluate(ws, expression) {
  return new Promise((resolve, reject) => {
    const id = Math.floor(Math.random() * 1e9);
    const onMessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.id !== id) return;
      ws.removeEventListener('message', onMessage);
      if (msg.error || msg.result.exceptionDetails) reject(new Error(JSON.stringify(msg.error || msg.result.exceptionDetails)));
      else resolve(msg.result.result.value);
    };
    ws.addEventListener('message', onMessage);
    ws.send(JSON.stringify({ id, method: 'Runtime.evaluate', params: { expression, awaitPromise: true, returnByValue: true } }));
  });
}

// The browser has extensions of its own: ours is the one whose background script is Pegaso's.
async function worker() {
  for (let i = 0; i < 40; ++i) {
    try {
      const targets = await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json();
      for (const target of targets.filter((t) => t.type === 'service_worker' && t.url.startsWith('chrome-extension://'))) {
        const ws = await connect(target);
        if ((await evaluate(ws, `typeof self.pegaso === 'object' && chrome.runtime.getManifest().name`)) === 'Pegaso') return { target, ws };
        ws.close();
      }
    } catch (e) {
      // the browser is not up yet
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error('the background script of the extension did not show up');
}

const { target, ws } = await worker();
const out = { extension: target.url };
out.withoutKey = await evaluate(ws, `self.pegaso.send(['http://127.0.0.1:1/x.bin']).then(() => 'sent', (e) => e.code)`);
await evaluate(ws, `chrome.storage.local.set({ token: ${JSON.stringify(key)}, port: ${Number(apiPort)} })`);
out.status = await evaluate(ws, `self.pegaso.call('GET', '/status').then((s) => s.version, (e) => 'error: ' + e.code + ' ' + e.detail)`);
out.sent = await evaluate(ws, `self.pegaso.send(${linksJson}, ${JSON.stringify(kind || '')} || undefined).then((a) => a, (e) => ({ error: e.code, detail: e.detail }))`);
out.wrongKey = await evaluate(ws, `chrome.storage.local.set({ token: 'ffffffffffffffffffffffffffffffff' }).then(() => self.pegaso.call('GET', '/status')).then(() => 'ok', (e) => e.code)`);
out.extract = await evaluate(ws, `self.pegaso.extractLinks('mira: magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=a, y https://example.org/a.zip. Fin')`);
out.menus = await evaluate(ws, `new Promise((r) => chrome.contextMenus.update('pegaso-link', {}, () => r(chrome.runtime.lastError ? chrome.runtime.lastError.message : 'ok')))`);
ws.close();
console.log(JSON.stringify(out));
