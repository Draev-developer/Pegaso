// Pegaso for the browser: what the background script, the popup and the options share.
// The extension talks to the local API of Pegaso (http://127.0.0.1:<port>/api/v1) with the
// key the user copies from Pegaso (Tools > Local API). Nothing is sent anywhere else.

export const ext = globalThis.browser ?? globalThis.chrome;

export const DEFAULTS = { port: 4780, token: '', capture: false };

export async function settings() {
  return { ...DEFAULTS, ...(await ext.storage.local.get(DEFAULTS)) };
}

export function t(key, ...args) {
  return ext.i18n.getMessage(key, args) || key;
}

// The links Pegaso can take which a text holds: ed2k, magnet and web addresses.
export function extractLinks(text) {
  const found = [];
  const patterns = [/ed2k:\/\/\|file\|[^\s"'<>]+/gi, /magnet:\?[^\s"'<>]+/gi, /https?:\/\/[^\s"'<>]+/gi];
  for (const re of patterns) {
    for (const m of String(text || '').matchAll(re)) {
      const link = m[0].replace(/[.,;)\]]+$/, '');
      if (!found.includes(link)) found.push(link);
    }
  }
  return found;
}

export class PegasoError extends Error {
  constructor(code, detail) {
    super(code);
    this.code = code; // needKey, notRunning, badKey, refused, failed
    this.detail = detail || '';
  }
}

// One call to the API. Throws a PegasoError which tells the user what to do.
export async function call(method, path, body) {
  const { port, token } = await settings();
  if (!token) throw new PegasoError('needKey');
  let response;
  try {
    response = await fetch(`http://127.0.0.1:${port}/api/v1${path}`, {
      method,
      headers: { Authorization: `Bearer ${token}`, ...(body ? { 'Content-Type': 'application/json' } : {}) },
      body: body ? JSON.stringify(body) : undefined,
    });
  } catch (e) {
    throw new PegasoError('notRunning');
  }
  let data = null;
  try {
    data = await response.json();
  } catch (e) {
    // an answer without a body
  }
  if (response.status === 401) throw new PegasoError('badKey');
  if (response.status === 403) throw new PegasoError('refused', data && data.error);
  if (!response.ok) throw new PegasoError('failed', (data && data.error) || `HTTP ${response.status}`);
  return data;
}

// Hands links over to Pegaso. kind 'torrent': the web addresses are those of .torrent files.
export async function send(links, kind) {
  const list = (Array.isArray(links) ? links : [links]).filter(Boolean);
  if (!list.length) throw new PegasoError('failed', t('noLinks'));
  const answer = await call('POST', '/downloads', kind ? { links: list, kind } : { links: list });
  if (!answer.added && answer.errors && answer.errors.length) throw new PegasoError('failed', answer.errors[0].error);
  return answer;
}

export function explain(error) {
  if (!(error instanceof PegasoError)) return String(error && error.message ? error.message : error);
  switch (error.code) {
    case 'needKey':
      return t('errNeedKey');
    case 'notRunning':
      return t('errNotRunning');
    case 'badKey':
      return t('errBadKey');
    case 'refused':
      return t('errRefused');
    default:
      return error.detail || t('errFailed');
  }
}

// fills the texts of a page: <span data-i18n="key">, placeholder with data-i18n-placeholder
export function localize(root = document) {
  for (const el of root.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n);
  for (const el of root.querySelectorAll('[data-i18n-placeholder]')) el.placeholder = t(el.dataset.i18nPlaceholder);
  document.documentElement.lang = ext.i18n.getUILanguage();
}
