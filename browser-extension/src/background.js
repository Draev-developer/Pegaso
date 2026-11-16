// Pegaso for the browser: the menu on links and selections, and - if the user wants it -
// the downloads of the browser handed over to Pegaso.
import * as lib from './lib.js';

const { ext, send, extractLinks, explain, settings, t } = lib;
self.pegaso = lib; // for the tests, which drive the extension from outside

function notify(title, message) {
  ext.notifications.create({ type: 'basic', iconUrl: ext.runtime.getURL('icons/icon-128.png'), title, message });
}

async function handOver(links, kind) {
  try {
    const answer = await send(links, kind);
    const failed = (answer.errors || []).length;
    notify(t('name'), failed ? t('sentSome', String(answer.added), String(failed)) : t('sent', String(answer.added)));
    return true;
  } catch (error) {
    notify(t('notSent'), explain(error));
    return false;
  }
}

ext.runtime.onInstalled.addListener(() => {
  ext.contextMenus.removeAll(() => {
    ext.contextMenus.create({ id: 'pegaso-link', title: t('menuLink'), contexts: ['link'] });
    ext.contextMenus.create({ id: 'pegaso-selection', title: t('menuSelection'), contexts: ['selection'] });
    ext.contextMenus.create({ id: 'pegaso-media', title: t('menuMedia'), contexts: ['video', 'audio'] });
  });
});

ext.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId === 'pegaso-link' && info.linkUrl) handOver([info.linkUrl]);
  else if (info.menuItemId === 'pegaso-media' && info.srcUrl) handOver([info.srcUrl]);
  else if (info.menuItemId === 'pegaso-selection') {
    const links = extractLinks(info.selectionText);
    if (links.length) handOver(links);
    else notify(t('notSent'), t('noLinks'));
  }
});

// A download the browser starts goes to Pegaso instead, when the user has asked for that.
// If Pegaso does not take it, the browser goes on with it as if nothing had happened.
ext.downloads.onCreated.addListener(async (item) => {
  const { capture } = await settings();
  const url = item.finalUrl || item.url || '';
  if (!capture || !/^https?:\/\//i.test(url)) return;
  const torrent = item.mime === 'application/x-bittorrent' || /\.torrent$/i.test(item.filename || '');
  if (await handOver([url], torrent ? 'torrent' : undefined)) {
    try {
      await ext.downloads.cancel(item.id);
      await ext.downloads.erase({ id: item.id });
    } catch (e) {
      // it was over already
    }
  }
});
