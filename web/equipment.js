/* Independent equipment UI; all inventory writes use server revision checks. */
(() => {
  'use strict';
  const grades = ['S', 'A', 'B', 'C', 'D', '待评级'];
  const descriptions = {S:'顶尖优先', A:'强力常用', B:'明确用途／替代', C:'少量用途', D:'当前基本无用途', '待评级':'依据不足'};
  const groups = {normal:'普通装备', special:'特殊装备', unknown:'获取渠道待核实'};
  const escape = x => String(x ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  let state, token, preview, busy = false;
  let filters = {scope:'owned', search:'', type:'', grade:'', group:''};
  const root = () => document.querySelector('#equipment-app');
  const el = s => root().querySelector(s);
  const url = x => /^https:\/\/wiki\.biligame\.com\/blhx\//.test(x || '') || /^https:\/\/www\.bilibili\.com\/opus\/\d+$/.test(x || '') ? x : '';
  function notice(message, error=false) {
    el('#eq-message').textContent = message;
    el('#eq-message').className = error ? 'warning' : 'note';
  }
  async function api(action, payload) {
    const response = await fetch('/api/equipment/' + action, payload === undefined ? {} : {
      method:'POST', headers:{'X-Azur-Token':token, 'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({payload:JSON.stringify(payload)})
    });
    const value = await response.json();
    if (!response.ok || value.ok === false) throw Error(value.error || '装备操作失败');
    return value;
  }
  function inventoryRows(id) { return state.document.entries.filter(r => r.equipmentId === id); }
  function collection() {
    const known = new Set(state.equipment.map(g => g.id));
    const unknown = [...new Map(state.document.entries.filter(r => !known.has(r.equipmentId)).map(r => [r.equipmentId, {
      id:r.equipmentId,name:r.name,type:'未识别',rarity:'未知',grade:'待评级',classification:'unknown',
      classificationReason:'图鉴尚未匹配此型号；保留原始库存，不推断强度或获取方式。',history:[],researchProducts:[],acquisition:[],aliases:[]
    }])).values()];
    return [...state.equipment,...unknown].map(g => ({...g, quantity:inventoryRows(g.id).reduce((a,r)=>a+r.quantity,0)}));
  }
  function sourceLink(source) {
    const href = url(source.url);
    return href ? `<a href="${escape(href)}" target="_blank" rel="noopener noreferrer">${escape(source.title)}</a>` : escape(source.title);
  }
  function shell() {
    const latestSource=[...state.sources].sort((a,b)=>b.date.localeCompare(a.date))[0];
    root().innerHTML = `<div class="section-title"><div><h2>我的装备分级表</h2><p>只统计仓库。评级不因强化或锁定改变，是否拆解由你决定。</p></div><button id="eq-reload" class="secondary">重新载入</button></div>
      <p class="eq-asof">最新已采用攻略：${escape(latestSource?.date || '暂无')} · ${escape(latestSource?.title || '待补充')}。未覆盖型号保持待评级；未上榜不等于无用。</p>
      <div class="eq-toolbar card"><div class="actions"><button id="eq-add">添加装备</button><button id="eq-export" class="secondary">导出库存</button><label class="eq-file">导入库存 JSON<input id="eq-file" type="file" accept=".json,application/json"></label></div><p id="eq-message" class="note" role="status" aria-live="polite">库存修改会保存到本机，覆盖前自动备份。</p><div id="eq-import"></div></div>
      <div class="filter-bar eq-filters"><label>范围<select id="eq-scope"><option value="owned">我的库存</option><option value="all">参考图鉴</option></select></label><label>搜索<input id="eq-search" type="search" placeholder="装备名、型号或别名"></label><label>类型<select id="eq-type"><option value="">全部类型</option>${[...new Set(state.equipment.map(g=>g.type))].sort().map(t=>`<option>${escape(t)}</option>`).join('')}</select></label><label>等级<select id="eq-grade"><option value="">全部等级</option>${grades.map(g=>`<option>${g}</option>`).join('')}</select></label><label>分栏<select id="eq-group"><option value="">全部</option>${Object.entries(groups).map(([id,name])=>`<option value="${id}">${name}</option>`).join('')}</select></label></div>
      <p id="eq-count" class="note"></p><div id="eq-board"></div>
      <details class="card eq-sources"><summary>评级规则、覆盖情况与历期依据</summary><p>S 顶尖优先；A 强力常用；B 有明确用途或替代价值；C 勉强有用；D 当前评估范围内基本用不到。等级是本工具对攻略的整理，不是攻略作者直接发布的 S—D 评分。同一等级不强行排先后。</p><p>以主线、活动、大世界、META 等 PVE 为主；演习、调速、特定舰船与配装单独说明。未上榜不等于 D，资料不足保留为待评级。研发材料的战斗评级不变，另标出有价值的研发成品。</p><p>联动、账号仅能获得一件的装备始终特殊；其他装备按已核实的当前常驻获取渠道分类。获取信息不完整时放入待核实区域。</p><p id="eq-coverage"></p>${state.notes.map(n=>`<p class="note">${escape(n)}</p>`).join('')}<ul>${state.sources.map(s=>`<li>${sourceLink(s)} · ${escape(s.date)}${s.localImage ? ` · <a href="/assets/equipment/${escape(s.localImage)}" target="_blank" rel="noopener">查看存档原图</a>`:''}</li>`).join('')}</ul><p class="note">参考资料读取日期：${escape(state.retrievedAt)}。只在你要求时更新，不自动联网。</p></details>
      <dialog id="eq-dialog" class="eq-dialog"><button class="secondary eq-close" type="button">关闭</button><div id="eq-dialog-body"></div></dialog>`;
    for (const key of Object.keys(filters)) {
      const node=el('#eq-'+key); node.value=filters[key];
      node.addEventListener(key==='search'?'input':'change',()=>{filters[key]=node.value;board();});
    }
    el('#eq-add').onclick=()=>editor();
    el('#eq-reload').onclick=()=>window.loadEquipment();
    el('#eq-export').onclick=()=>task(async()=>{
      const result=await api('export',{});
      const blob=new Blob([JSON.stringify(result.document,null,2)],{type:'application/json'});
      const download=URL.createObjectURL(blob); const a=document.createElement('a');a.href=download;a.download=result.filename;a.click();setTimeout(()=>URL.revokeObjectURL(download),1000);
      notice('库存已导出。导出文件只含库存，不会覆盖另一端的攻略评级。');
    });
    el('#eq-file').onchange=e=>task(async()=>{
      preview=null;el('#eq-import').innerHTML='';const file=e.target.files[0];if(!file)return;
      if(file.size>4*1024*1024)throw Error('文件超过 4 MB');
      let document;try{document=JSON.parse((await file.text()).replace(/^\uFEFF/,''));}catch{throw Error('文件不是有效的 JSON');}
      preview=await api('import-preview',{document,revision:state.revision});
      el('#eq-import').innerHTML=`<div class="eq-preview"><b>导入预览 · 替换整份仓库库存</b><p>当前 ${preview.oldCount} 件 → 导入后 ${preview.newCount} 件；新增 ${preview.added} 条，修改 ${preview.changed} 条，移除 ${preview.removed} 条。</p>${preview.unknown.length?`<p class="warning">${preview.unknown.length} 种装备未匹配图鉴，将保留在待评级／待核实区域：${preview.unknown.map(escape).join('、')}</p>`:''}<button id="eq-apply">确认替换并备份原库存</button> <button id="eq-cancel-import" class="secondary">取消</button></div>`;
      el('#eq-apply').onclick=()=>task(async()=>{state=await api('import-apply',{previewId:preview.previewId,revision:state.revision});preview=null;shell();board();notice('导入完成，原库存已自动备份。');});
      el('#eq-cancel-import').onclick=()=>{preview=null;el('#eq-import').innerHTML='';el('#eq-file').value='';};
    });
    el('.eq-close').onclick=()=>el('#eq-dialog').close();
  }
  async function task(fn) {
    if(busy)return;busy=true;
    root().querySelectorAll('button,input[type=file]').forEach(b=>b.disabled=true);
    try{await fn();}catch(error){notice(error.message,true);const errorBox=el('#eq-edit-error');if(errorBox)errorBox.textContent=error.message;}
    finally{busy=false;root().querySelectorAll('button,input[type=file]').forEach(b=>b.disabled=false);}
  }
  function card(g) {
    return `<button class="eq-card eq-${escape(g.grade==='待评级'?'pending':g.grade)}" data-eq-id="${escape(g.id)}">${g.image?`<img src="/assets/equipment/${escape(g.image)}" alt="" loading="lazy">`:`<span class="eq-icon">${escape(g.type==='未识别'?'?':g.type.slice(0,2))}</span>`}<span><strong>${escape(g.name)}</strong><small>${escape(g.rarity)} · ${escape(g.type)}${g.quantity?` · 库存 ${g.quantity}`:' · 未录入'}</small>${g.classification==='special'?`<em>${escape(g.specialReason || '特殊装备')}</em>`:''}${g.usefulMaterial?'<em class="eq-material">可研发高价值装备</em>':''}${g.assessment?.tags?.length?`<small>${g.assessment.tags.map(escape).join(' · ')}</small>`:''}</span></button>`;
  }
  function board() {
    const all=collection(); const q=filters.search.trim().toLowerCase();
    const items=all.filter(g=>(filters.scope==='all'||g.quantity>0)&&(!q||[g.name,g.id,...(g.aliases||[])].some(s=>s.toLowerCase().includes(q)))&&(!filters.type||g.type===filters.type)&&(!filters.grade||g.grade===filters.grade)&&(!filters.group||g.classification===filters.group));
    const count=state.document.entries.reduce((a,r)=>a+r.quantity,0);
    el('#eq-count').textContent=`仓库共 ${count} 件 · 当前显示 ${items.length} 种型号 · 点击装备查看评级依据或修改数量`;
    document.querySelector('#status').textContent=`${count} 件仓库装备`;
    const shownGrades=grades.filter(g=>!filters.grade||g===filters.grade);
    const columns=['normal','special'].filter(g=>!filters.group||filters.group===g);
    let html='';
    if(columns.length)html=`<div class="eq-table-wrap"><table class="eq-table"><thead><tr><th scope="col">等级</th>${columns.map(c=>`<th scope="col">${groups[c]}${c==='special'?'<small>联动／唯一／当前活动限定</small>':'<small>已核实可常驻获取</small>'}</th>`).join('')}</tr></thead><tbody>${shownGrades.map(grade=>`<tr><th scope="row" class="eq-grade eq-${grade==='待评级'?'pending':grade}"><b>${grade}</b><small>${descriptions[grade]}</small></th>${columns.map(group=>{const list=items.filter(g=>g.grade===grade&&g.classification===group);return `<td><div class="eq-items">${list.map(card).join('')||'<span class="eq-empty">暂无装备</span>'}</div></td>`;}).join('')}</tr>`).join('')}</tbody></table></div>`;
    const unknown=items.filter(g=>g.classification==='unknown');
    if(unknown.length)html+=`<section class="card eq-unverified"><h3>获取渠道待核实 · ${unknown.length} 种</h3><p class="note">这里的装备不会混入普通装备栏。强度和获取渠道分别判断。</p>${shownGrades.map(grade=>{const list=unknown.filter(g=>g.grade===grade);return list.length?`<h4>${grade} · ${descriptions[grade]}</h4><div class="eq-items">${list.map(card).join('')}</div>`:'';}).join('')}</section>`;
    if(!items.length)html=`<div class="card eq-empty-state"><h3>${filters.scope==='owned'&&!count?'还没有录入仓库装备':'没有符合条件的装备'}</h3><p>${filters.scope==='owned'&&!count?'使用“添加装备”或“导入库存 JSON”。也可以切换到“参考图鉴”先查看评级。':'试试调整搜索和筛选条件。'}</p></div>`+html;
    el('#eq-board').innerHTML=html;
    el('#eq-coverage').textContent=`本地图鉴 ${state.equipment.length} 种型号，其中 ${state.equipment.filter(g=>g.grade!=='待评级').length} 种已评级，${state.equipment.filter(g=>g.classification==='unknown').length} 种获取渠道待核实。`;
    el('#eq-board').querySelectorAll('[data-eq-id]').forEach(b=>b.onclick=()=>detail(b.dataset.eqId));
  }
  function showDialog(html) {el('#eq-dialog-body').innerHTML=html;if(!el('#eq-dialog').open)el('#eq-dialog').showModal();}
  function detail(id) {
    const g=collection().find(g=>g.id===id);const rows=inventoryRows(id);
    const sourceById=Object.fromEntries(state.sources.map(s=>[s.id,s]));
    showDialog(`<h2>${escape(g.name)}</h2><p><span class="badge">${escape(g.grade)}</span> ${escape(groups[g.classification])} · ${escape(g.rarity)} · ${escape(g.type)}</p><p>${escape(g.assessment?.reason || '当前资料不足，不根据稀有度或是否上榜推断等级。')}</p>${g.assessment?.tags?.length?`<p>适用／限制：${g.assessment.tags.map(escape).join('、')}</p>`:''}
      <h3>获取与分类</h3><p>${escape(g.classificationReason)}</p><ul>${(g.acquisition||[]).map(a=>`<li>${escape(a)}</li>`).join('')}</ul>${url(g.wikiUrl)?`<p><a href="${escape(g.wikiUrl)}" target="_blank" rel="noopener">装备详情 WIKI</a></p>`:''}${[...new Set(g.acquisitionSources||[])].filter(url).map(href=>`<p><a href="${escape(href)}" target="_blank" rel="noopener">获取渠道核对依据</a></p>`).join('')}
      <h3>研发用途</h3>${g.researchProducts.length?`<ul>${g.researchProducts.map(t=>`<li>${escape(t.name)} · ${escape(t.grade)}</li>`).join('')}</ul><p>${g.usefulMaterial?'可作为 S／A／B 装备研发链的材料；上面的等级仍是本件装备自身的战斗评级。':'当前没有已确认的 S／A／B 研发成品；待评级成品不视为无用。'}</p>`:`<p class="note">${g.researchVerified?'已核实的资料中没有后续研发路线。':'研发路线尚待核实，不能据此断言没有材料用途。'}</p>`}
      <h3>我的库存</h3><p>总计 ${g.quantity} 件。这里的修改只更新本地记录。</p><div>${rows.map((r,i)=>`<div class="eq-stock-row"><span>${r.quantity} 件 · ${r.enhancement===null?'强化未知':'+'+r.enhancement}${r.locked?' · 已锁定':''}${r.note?' · '+escape(r.note):''}</span><button class="secondary" data-edit-row="${i}">修改</button></div>`).join('')||'<p class="note">尚未录入。</p>'}</div><button id="eq-add-detail">添加此装备</button>
      <h3>历期依据</h3>${g.history.length?[...g.history].reverse().map(h=>`<article class="eq-history"><b>${escape(h.grade)} · ${escape(sourceById[h.sourceId].date)}</b><p>${escape(h.reason)}</p><p>${sourceLink(sourceById[h.sourceId])}</p></article>`).join(''):'<p class="note">尚无已核对的评级依据。</p>'}`);
    el('#eq-add-detail').onclick=()=>editor(id);
    el('#eq-dialog-body').querySelectorAll('[data-edit-row]').forEach(b=>b.onclick=()=>editor(id,rows[Number(b.dataset.editRow)]));
  }
  function editor(id='', original=null) {
    const gear=collection().find(g=>g.id===id);
    showDialog(`<h2>${original?'修改库存记录':'添加仓库装备'}</h2><form id="eq-editor"><label>装备型号<input id="eq-pick" list="eq-options" required value="${escape(id)}" ${id&&!original?'readonly':''} placeholder="输入完整名称并选择型号"><datalist id="eq-options">${state.equipment.map(g=>`<option value="${escape(g.id)}">${escape(g.rarity+' · '+g.type)}</option>`).join('')}</datalist></label><p class="note">请选择完整型号（例如 T0／T3）。图鉴没有的装备也可按完整名称录入，暂列待评级。</p><div class="eq-form-grid"><label>数量<input id="eq-qty" type="number" min="${original?0:1}" max="999999" step="1" required value="${original?.quantity||1}"></label><label>强化等级<select id="eq-level"><option value="">未知／未区分</option>${Array.from({length:14},(_,n)=>`<option value="${n}">+${n}</option>`).join('')}</select></label></div><label class="eq-check"><input id="eq-locked" type="checkbox" ${original?.locked?'checked':''}> 已锁定（记录状态，不改变评级）</label><label>备注<input id="eq-note" maxlength="500" value="${escape(original?.note||'')}"></label><p class="note">${original?'数量设为 0 可移除此条本地记录，不操作游戏。':'已有相同型号、强化等级和锁定状态的记录时，会累加数量。'}</p><p id="eq-edit-error" class="warning" role="alert"></p><button type="submit">保存到库存</button></form>`);
    el('#eq-level').value=original?.enhancement==null?'':String(original.enhancement);
    el('#eq-editor').onsubmit=e=>{e.preventDefault();task(async()=>{
      const eid=el('#eq-pick').value.trim();if(!eid)throw Error('请选择或填写装备型号');
      const known=state.equipment.find(g=>g.id===eid);
      const quantity=Number(el('#eq-qty').value), level=el('#eq-level').value===''?null:Number(el('#eq-level').value), locked=el('#eq-locked').checked;
      if(!Number.isInteger(quantity)||quantity<(original?0:1))throw Error('数量必须是有效整数');
      const entries=state.document.entries.filter(r=>r!==original).map(r=>({...r}));
      if(quantity){const matching=entries.find(r=>r.equipmentId===eid&&r.enhancement===level&&r.locked===locked);
        if(matching){matching.quantity+=quantity;if(el('#eq-note').value)matching.note=el('#eq-note').value;}
        else entries.push({equipmentId:eid,name:known?.name||gear?.name||eid,quantity,enhancement:level,locked,note:el('#eq-note').value});}
      state=await api('save',{revision:state.revision,document:{...state.document,entries}});
      el('#eq-dialog').close();preview=null;shell();board();notice('库存已保存。');
    });};
  }
  window.loadEquipment=async()=>{
    if(busy)return;
    try{token=(await (await fetch('/api/app/snapshot')).json()).token;state=await api('snapshot');shell();board();}
    catch(error){root().textContent='装备数据载入失败：'+error.message;throw error;}
  };
})();
