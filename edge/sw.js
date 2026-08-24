/* viki edge service worker.
 *
 * TWO CACHES ON PURPOSE. The shell is small and must be precached so the app
 * opens with no network at all. The MODEL is ~23MB and onnxruntime another
 * ~10MB: precaching those on install would make first visit a 33MB download
 * before anything appears, on a phone, possibly on cellular. They are cached
 * on first successful use instead, so the app is useful (BM25 + literal)
 * immediately and becomes hybrid once the weights have arrived once.
 *
 * The cache.db is NOT handled here — it lives in OPFS, written by the page,
 * because it is data the user pulled rather than an asset we ship, and it must
 * survive a service-worker cache version bump.
 *
 * iOS NOTE: Safari supports service workers, but only a HOME-SCREEN-INSTALLED
 * PWA is exempt from the 7-day eviction of script-writable storage. That is
 * why the page pushes install so hard on iOS: on iOS, "install" is not a
 * convenience, it is what makes offline durable.
 */
const SHELL = 'viki-shell-v1';
const HEAVY = 'viki-heavy-v1';

const SHELL_FILES = [
  './', './index.html', './viki-edge.js', './viki-edge.wasm',
  './manifest.webmanifest', './icons/icon-180.png', './icons/icon-192.png',
];

self.addEventListener('install', e => {
  // addAll fails the whole install if ANY file 404s, which is what we want:
  // a half-cached shell that opens broken offline is worse than not installing.
  e.waitUntil(caches.open(SHELL).then(c => c.addAll(SHELL_FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys()
      .then(ks => Promise.all(ks.filter(k => k !== SHELL && k !== HEAVY).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

const isHeavy = url => /\.(onnx|wasm)$/.test(url.pathname) && !url.pathname.endsWith('viki-edge.wasm')
               || /ort.*\.wasm$/.test(url.pathname) || url.pathname.endsWith('vocab.txt')
               || url.pathname.endsWith('ort.min.js');

self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== location.origin) return;

  // Never cache the pulled corpus: it is refreshed deliberately, and a stale
  // copy served from here would silently contradict what the registry says.
  if (url.pathname.endsWith('cache.db')) return;

  const bucket = isHeavy(url) ? HEAVY : SHELL;
  e.respondWith(
    caches.match(req).then(hit => hit || fetch(req).then(res => {
      if (res && res.ok && res.type === 'basic') {
        const copy = res.clone();
        caches.open(bucket).then(c => c.put(req, copy));
      }
      return res;
    }).catch(() => hit))
  );
});
