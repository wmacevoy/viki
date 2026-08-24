import fs from 'fs';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const VikiEdge = require('/Users/wmacevoy/projects/viki/edge/dist/viki-edge-sqlcipher.js');
const S = '/private/tmp/claude-501/-Users-wmacevoy-projects-viki/ff137b62-22e0-47b5-84a9-b8a54c312a8b/scratchpad';
const M = await VikiEdge();
const C = (f,r,t,a) => M.ccall(f,r,t,a);
const t = (n,ok) => console.log(`  ${ok?'PASS':'FAIL'}  ${n}`);

const tribes = ['sqlcipher-libressl','strata','fossil-sqlcipher-libressl'];
console.log('V-series: one edge, three tribes, three DIFFERENT keys\n');
for (const name of tribes) {
  const key = fs.readFileSync(`${S}/verse/${name}.key`,'utf8').trim();
  M.FS.writeFile(`/${name}.db`, fs.readFileSync(`${S}/verse/${name}.enc.db`));
  const slot = C('edge_tribe_add','number',['string','string','string'],[name, `/${name}.db`, key]);
  t(`V1 ${name} opens with its own key (slot ${slot}, ${C('edge_tribe_chunks','number',['number'],[slot])} chunks)`, slot >= 0);
}
t(`V2 the edge holds ${C('edge_tribe_count','number',[],[])} tribes at once`,
  C('edge_tribe_count','number',[],[]) === 3);

// CONTROL: tribe A's key must not open tribe B
const wrongKey = fs.readFileSync(`${S}/verse/strata.key`,'utf8').trim();
const bad = C('edge_tribe_add','number',['string','string','string'],['x','/sqlcipher-libressl.db', wrongKey]);
t('V3 CONTROL: another tribe’s key does NOT open this one', bad < 0);

console.log('\nV4: the question that would have saved me this morning');
const p = C('edge_ask_all','number',['string','number'],
            ['persisting a database in the browser origin private file system OPFS', 5]);
const r = JSON.parse(M.UTF8ToString(p)); C('edge_free',null,['number'],[p]);
console.log(`     mode=${r.mode}  tribes=${r.tribes}`);
for (const h of r.results)
  console.log(`     [${h.rank}] rrf=${h.rrf.toFixed(4)}  ${h.tribe.padEnd(26)} ${h.source}`);
t('V5 the answer comes from a tribe that is NOT the one being worked in',
  r.results.length > 0 && r.results[0].tribe === 'sqlcipher-libressl');
t('V6 every hit is attributed to a tribe',
  r.results.every(h => typeof h.tribe === 'string' && h.tribe.length > 0));
