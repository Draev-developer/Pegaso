import { ext, call, settings, explain, localize, t, DEFAULTS } from './lib.js';

const $ = (id) => document.getElementById(id);

function say(text, bad) {
  $('message').hidden = !text;
  $('message').textContent = text || '';
  $('message').classList.toggle('bad', !!bad);
}

async function load() {
  const s = await settings();
  $('token').value = s.token;
  $('port').value = s.port;
  $('capture').checked = s.capture;
}

$('form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const port = parseInt($('port').value, 10);
  await ext.storage.local.set({
    token: $('token').value.trim(),
    port: port >= 1 && port <= 65535 ? port : DEFAULTS.port,
    capture: $('capture').checked,
  });
  try {
    const status = await call('GET', '/status');
    say(t('testOk', status.version));
  } catch (error) {
    say(explain(error), true);
  }
});

localize();
load();
