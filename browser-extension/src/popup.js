import { ext, call, send, extractLinks, explain, localize, t } from './lib.js';

const $ = (id) => document.getElementById(id);

function say(text, bad) {
  $('message').hidden = !text;
  $('message').textContent = text || '';
  $('message').classList.toggle('bad', !!bad);
}

function size(bytes) {
  if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + ' GB';
  if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
  return Math.max(1, Math.round(bytes / 1024)) + ' KB';
}

async function refresh() {
  try {
    const status = await call('GET', '/status');
    $('state').textContent = t('connectedTo', status.version);
    const { downloads } = await call('GET', '/downloads');
    $('count').textContent = downloads.length ? t('downloads', String(downloads.length)) : t('noDownloads');
    const list = $('list');
    list.replaceChildren();
    for (const d of downloads.slice(0, 30)) {
      const li = document.createElement('li');
      const name = document.createElement('div');
      name.className = 'name';
      name.textContent = d.name;
      name.title = d.name;
      const percent = d.size ? Math.min(100, Math.floor((d.completed * 100) / d.size)) : 0;
      const bar = document.createElement('div');
      bar.className = 'bar';
      const fill = document.createElement('div');
      fill.style.width = percent + '%';
      bar.append(fill);
      const info = document.createElement('div');
      info.className = 'muted small';
      info.textContent = `${percent} %  ·  ${size(d.size)}  ·  ${d.status}`;
      li.append(name, bar, info);
      list.append(li);
    }
  } catch (error) {
    $('state').textContent = t('notConnected');
    $('count').textContent = '';
    $('list').replaceChildren();
    say(explain(error), true);
  }
}

$('add').addEventListener('submit', async (event) => {
  event.preventDefault();
  const links = extractLinks($('link').value);
  if (!links.length) {
    say(t('noLinks'), true);
    return;
  }
  try {
    const answer = await send(links);
    say(t('sent', String(answer.added)));
    $('link').value = '';
    refresh();
  } catch (error) {
    say(explain(error), true);
  }
});

$('options').addEventListener('click', () => ext.runtime.openOptionsPage());

localize();
refresh();
setInterval(refresh, 2000);
