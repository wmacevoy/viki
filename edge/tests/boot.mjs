// Replays the page's boot sequence in node against the REAL dist bundle, so
// "stuck at loading" cannot recur unnoticed without a browser.
import fs from 'fs';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const D='/Users/wmacevoy/projects/viki/edge/dist';
const M = await require(D+'/viki-edge.js')();
const C=(f,r,t,a)=>M.ccall(f,r,t,a);
let ok=0, bad=0;
const t=(n,c)=>{ (c?ok++:bad++); console.log(`  ${c?'PASS':'FAIL'}  ${n}`); };

// every symbol the page calls must exist in the module it actually loads
for (const fn of ['edge_tribe_add','edge_tribe_chunks','edge_ask_all','edge_free',
                  'edge_vocab_load','edge_tokenize','edge_set_query_from_hidden',
                  'edge_clear_query_vector'])
  t(`P1 ${fn} is exported`, typeof M['_'+fn] === 'function');

M.FS.writeFile('/c.db', fs.readFileSync(D+'/cache.db'));
const slot=C('edge_tribe_add','number',['string','string','string'],['local','/c.db','']);
t('P2 the shipped cache.db opens as a tribe', slot>=0);
const n=C('edge_tribe_chunks','number',['number'],[slot]);
t(`P3 it holds chunks (${n})`, n>0);
const p=C('edge_ask_all','number',['string','number'],['fossil',3]);
const r=JSON.parse(M.UTF8ToString(p)); C('edge_free',null,['number'],[p]);
t(`P4 ask_all answers (${r.results.length} hits, mode=${r.mode})`, r.results.length>0);
t('P5 hits carry a tribe label', r.results.every(h=>h.tribe));
console.log(`\nPASS=${ok} FAIL=${bad}`);
process.exit(bad?1:0);
