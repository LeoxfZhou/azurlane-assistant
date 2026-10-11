const fs=require('fs'),vm=require('vm'),assert=require('assert');
let source=fs.readFileSync(__dirname+'/../web/fleet_plans.js','utf8');
source=source.replace(/\}\)\(\);\s*$/, 'globalThis.testApi={recommend,renderSaved,localTime,setData:d=>{data=d;selected=d.document.plans[0].id;}};})();');
const nodes=new Map();
function node(id){if(!nodes.has(id))nodes.set(id,{textContent:'',innerHTML:'',disabled:false,classList:{toggle(){}},querySelectorAll:()=>[node('#fleet-recommend')]});return nodes.get(id);}
const calls=[];
let fail=false;
const context={
 document:{querySelector:node},window:{addEventListener(){}},
 clearTimeout(){},setTimeout(){},console,
 esc:x=>String(x),state:{inventory:[]},
 requestJson:async(path,body)=>{
   const id=JSON.parse(body.payload).planId;calls.push(id);
   if(fail)throw Error('test failure');
   return {recommendations:{first:{calculatedAt:'2026-10-06T07:39:25Z',result:{teams:[]}},second:{calculatedAt:'2026-10-06T07:39:25Z',result:{teams:[]}}}};
 }
};
vm.runInNewContext(source,context);
(async()=>{
 assert.strictEqual(node('#fleet-export').disabled,true);
 node('#fleet-export-plans').checked=true;node('#fleet-export-plans').onchange();
 assert.strictEqual(node('#fleet-export').disabled,false);
 node('#fleet-export-plans').checked=false;node('#fleet-export-inventory').checked=true;node('#fleet-export-inventory').onchange();
 assert.strictEqual(node('#fleet-export').disabled,false);
 node('#fleet-export-inventory').checked=false;node('#fleet-export-inventory').onchange();
 assert.strictEqual(node('#fleet-export').disabled,true);
 await node('#fleet-export').onclick();assert.strictEqual(calls.length,0);
 const api=context.testApi;
 api.setData({document:{plans:[{id:'first',name:'主线'},{id:'second',name:'大世界'}]},recommendations:{}});
 await api.recommend();
 assert.deepStrictEqual(calls,['first']);
 assert(node('#fleet-save-status').textContent.includes('全部 2 个方案'));
 assert.strictEqual(node('#fleet-recommend').disabled,false);
 assert.strictEqual(node('#fleet-recommend').textContent,'重新推荐所有方案');
 assert(node('#fleet-results').innerHTML.includes('fleet-result-meta'));
 assert(node('#fleet-results').innerHTML.includes('fleet-result-grid'));
 assert(!node('#fleet-results').innerHTML.includes('T07:39'));
 fail=true;calls.length=0;await api.recommend();
 assert.deepStrictEqual(calls,['first']);
 assert(node('#fleet-save-status').textContent.includes('所有原推荐均保留'));
 assert.strictEqual(node('#fleet-recommend').disabled,false);
 assert.strictEqual(api.localTime('invalid'),'时间未知');
 console.log('Fleet UI batching, failure recovery and layout markup passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
