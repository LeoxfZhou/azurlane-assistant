/* Personal plans are never part of Windows inventory imports. */
(() => {
  let data = null, selected = '', timer = null, dirty = false, saving = false, generation = 0;
  let blocked = false, initializing = false;
  let theoryGeneration = 0, recommending = false;
  const localTime = value => { const date=new Date(value); if(Number.isNaN(date.getTime()))return '时间未知'; const pad=n=>String(n).padStart(2,'0');return `${date.getFullYear()}-${pad(date.getMonth()+1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`; };
  const goals = {damage:'自动输出', survival:'生存优先', speed:'速刷／航速', torpedo:'雷击专项', antisub:'防潜专项'};
  const uid = () => crypto.randomUUID();
  const plan = () => data?.document.plans.find(p => p.id === selected);
  const getShip = id => data.ships.find(s => s.id === id);
  const root = () => document.querySelector('#fleet-app');
  const say = (text, error=false) => {
    const node = document.querySelector('#fleet-save-status');
    if (node) { node.textContent=text; node.className=error?'warning':'note'; }
    document.querySelector('#fleet-retry')?.classList.toggle('hidden',!error);
    document.querySelector('#fleet-reload')?.classList.toggle('hidden',!error);
  };
  const endpoint = '/api/commander-cats/fleets';
  async function save() {
    clearTimeout(timer);
    if (!dirty || saving || blocked) return;
    if(data.document.plans.some(p=>!p.name.trim()||p.teams.some(t=>!t.name.trim()))) {
      say('请补全方案名和队名；当前编辑仍保留在页面中',true);return;
    }
    saving = true;
    const sent = generation;
    say('正在自动保存…');
    try {
      const result = await requestJson(endpoint+'/save', {payload:JSON.stringify({revision:data.revision,document:data.document})});
      data.revision = result.revision;
      data.recommendations=result.recommendations;
      renderSaved();
      void renderTheoryPlans();
      dirty = sent !== generation;
      say(dirty?'还有修改等待保存…':'已自动保存到本机');
    } catch(error) {
      say('保存失败，修改仍在当前页面：'+error.message,true);
      blocked = true;
    } finally {
      saving = false;
      if (dirty && !blocked) await save();
    }
  }
  function changed() {
    dirty = true; generation++;
    const results=document.querySelector('#fleet-results');
    if(results)renderSaved(true);
    say('修改待自动保存…');
    clearTimeout(timer);timer=setTimeout(save,400);
  }
  async function load(force=false) {
    if(dirty||saving||recommending)return;
    if (data && !force) {renderSaved();return;}
    try {
      const response=await fetch(endpoint);
      const payload=await response.json();
      if(!response.ok)throw new Error(payload.error||'读取编队失败');
      data=payload;blocked=!!data.warning;dirty=false;
      if(!plan())selected=data.document.plans[0]?.id||'';
      render();
      say(data.warning||`已载入 ${data.ships.length} 条舰娘／形态资料；本机自动保存`,!!data.warning);
      void renderTheoryPlans();
      await initializeRecommendations();
    } catch(error) {root().innerHTML=`<p class="warning">${esc(error.message)}</p><button id="fleet-load-retry">重新读取</button>`;document.querySelector('#fleet-load-retry').onclick=()=>load(true);}
  }
  window.loadFleetPlans=load;
  function render() {
    const p=plan();
    root().innerHTML=`<article class="card"><div class="actions"><select id="fleet-plan-select" aria-label="编队方案">${data.document.plans.map(x=>`<option value="${esc(x.id)}" ${x.id===selected?'selected':''}>${esc(x.name)}</option>`).join('')}</select><button id="fleet-plan-add">新增方案</button><button id="fleet-plan-delete" class="secondary" ${!p?'disabled':''}>删除方案</button><button id="fleet-reload" class="secondary hidden">重新载入</button><button id="fleet-retry" class="secondary hidden">重试保存</button></div><p id="fleet-save-status" class="note"></p>${p?`<div class="fleet-plan-toolbar"><label>方案名 <input id="fleet-plan-name" value="${esc(p.name)}" maxlength="60"></label><label>场景 <select id="fleet-plan-mode"><option value="opsi" ${p.mode==='opsi'?'selected':''}>大世界</option><option value="campaign" ${p.mode==='campaign'?'selected':''}>主线</option></select></label><button id="fleet-team-add">添加水面队</button><button id="fleet-sub-add" class="secondary">添加潜艇队</button></div>`:'<p>先新增一个方案，例如“大世界日常”。无需先编队，也能查看库存天赋等级。</p>'}</article><div id="fleet-teams" class="fleet-edit-grid"></div>${p?'<button id="fleet-recommend">重新推荐所有方案</button>':''}<p class="note">规则推荐：先保证队伍覆盖，再按数字弱优先级小幅交换；数字越小优先。同方案内实例去重，跨方案复用时统一培养路线；高优先级决定方向，同级路线冲突需调整优先级。不是全局数学最优或真实伤害模拟。空队暂不分配。等级与已洗成果不参与计算。</p><div id="fleet-results"></div>`;
    document.querySelector('#fleet-plan-select').onchange=e=>{selected=e.target.value;render();renderSaved();};
    document.querySelector('#fleet-plan-add').onclick=()=>{
      if(blocked)return say('当前存档有错误，请先重新载入或处理原文件',true);
      if(data.document.plans.length>=30)return say('最多30个方案',true);
      const value={id:uid(),name:'新方案',mode:'opsi',teams:[]};
      data.document.plans.push(value);selected=value.id;render();changed();
    };
    document.querySelector('#fleet-plan-delete').onclick=()=>{
      if(!p||!confirm(`删除方案“${p.name}”？不会删除库存猫或舰娘资料。`))return;
      data.document.plans=data.document.plans.filter(x=>x.id!==p.id);selected=data.document.plans[0]?.id||'';render();changed();
    };
    document.querySelector('#fleet-reload').onclick=()=>{
      if((dirty||saving)&&!confirm('有未保存修改，确定放弃并重新载入？'))return;
      if(saving)return say('请等待当前保存完成',true);
      clearTimeout(timer);load(true);
    };
    document.querySelector('#fleet-retry').onclick=()=>{if(data.warning)return say(data.warning,true);blocked=false;save();};
    if(!p)return;
    document.querySelector('#fleet-plan-name').oninput=e=>{p.name=e.target.value;changed();};
    document.querySelector('#fleet-plan-mode').onchange=e=>{p.mode=e.target.value;changed();};
    const addTeam=kind=>{
      if(p.teams.length>=8)return say('每个方案最多8队',true);
      p.teams.push({id:uid(),name:`第${p.teams.length+1}队`,kind,goal:'damage',priority:p.teams.length+1,slots:Array(kind==='surface'?6:3).fill('')});render();changed();
    };
    document.querySelector('#fleet-team-add').onclick=()=>addTeam('surface');
    document.querySelector('#fleet-sub-add').onclick=()=>addTeam('submarine');
    document.querySelector('#fleet-teams').innerHTML=p.teams.map((team,ti)=>`<article class="card"><div class="fleet-team-toolbar"><input data-team-name="${ti}" aria-label="队伍名称" maxlength="60" value="${esc(team.name)}"><label>优先级 <input type="number" min="1" max="99" data-team-priority="${ti}" value="${team.priority||ti+1}" aria-label="优先级编号"></label><select data-team-goal="${ti}" aria-label="用途">${Object.entries(goals).map(([key,label])=>`<option value="${key}" ${key===team.goal?'selected':''}>${label}</option>`).join('')}</select><button data-team-delete="${ti}" class="secondary fleet-delete" title="删除队伍">删除</button></div><div class="ship-slots">${team.slots.map((id,slot)=>{
      const ship=getShip(id);const label=team.kind==='submarine'?`潜艇${slot+1}`:slot<3?`后排${slot===0?'旗舰':slot+1}`:`前排${slot-2}`;
      return `<button data-slot="${ti}:${slot}" class="ship-slot"><small>${label}</small>${ship?`<img loading="lazy" src="${esc(ship.image)}" onerror="this.src='/assets/commander_cats/placeholder.svg'"><b>${esc(ship.displayName)}</b><small>${esc(ship.faction)} · ${esc(ship.type)}</small>`:'<span>＋ 选择舰娘</span>'}</button>`;
    }).join('')}</div></article>`).join('');
    root().querySelectorAll('[data-team-name]').forEach(el=>el.oninput=()=>{p.teams[+el.dataset.teamName].name=el.value;changed();});
    root().querySelectorAll('[data-team-priority]').forEach(el=>{
      el.oninput=()=>{const n=Number(el.value);if(!Number.isInteger(n)||n<1||n>99)return say('优先级须为1—99的整数；暂不保存该输入',true);p.teams[+el.dataset.teamPriority].priority=n;changed();};
      el.onblur=()=>{el.value=p.teams[+el.dataset.teamPriority].priority||1;};
    });
    root().querySelectorAll('[data-team-goal]').forEach(el=>el.onchange=()=>{p.teams[+el.dataset.teamGoal].goal=el.value;changed();});
    root().querySelectorAll('[data-team-delete]').forEach(el=>el.onclick=()=>{if(confirm('删除这支队伍？')){p.teams.splice(+el.dataset.teamDelete,1);render();changed();}});
    root().querySelectorAll('[data-slot]').forEach(el=>el.onclick=()=>{const [ti,si]=el.dataset.slot.split(':').map(Number);chooseShip(p.teams[ti],si);});
    document.querySelector('#fleet-recommend').onclick=recommend;
    renderSaved();
  }
  function chooseShip(team,slot) {
    const dialog=document.createElement('dialog');dialog.className='fleet-ship-picker';
    dialog.innerHTML='<div class="section-title"><h3>选择舰娘</h3><button type="button" data-close class="secondary">关闭</button></div><input type="search" aria-label="搜索舰娘" placeholder="原名、和谐名、阵营或舰种"><button type="button" data-empty class="secondary">留空此位置</button><div class="ship-search-results"></div>';
    document.body.append(dialog);dialog.showModal();
    const close=()=>{dialog.close();dialog.remove();};
    dialog.querySelector('[data-close]').onclick=close;
    dialog.addEventListener('cancel',e=>{e.preventDefault();close();});
    dialog.querySelector('[data-empty]').onclick=()=>{team.slots[slot]='';close();render();changed();};
    const expected=team.kind==='submarine'?'潜艇':slot<3?'后排':'前排';
    const occupied=new Set(plan().teams.flatMap(t=>t.slots).filter(id=>id&&id!==team.slots[slot]).map(id=>getShip(id)?.baseId));
    function search() {
      const query=dialog.querySelector('input').value.trim().toLowerCase();
      const results=data.ships.filter(s=>s.groups.includes(expected)&&[s.name,s.harmonyName,s.displayName,s.faction,s.type].some(v=>v.toLowerCase().includes(query)));
      const area=dialog.querySelector('.ship-search-results');
      area.innerHTML=`<p class="note">${results.length} 条结果；同方案已使用舰娘不能重复。列表最多显示100条，请输入名称缩小范围。</p>`+results.slice(0,100).map(ship=>`<button data-ship="${esc(ship.id)}" ${occupied.has(ship.baseId)?'disabled':''}><img loading="lazy" src="${esc(ship.image)}" onerror="this.src='/assets/commander_cats/placeholder.svg'"><span>${esc(ship.displayName)}<small>${esc(ship.faction)} · ${esc(ship.type)}${occupied.has(ship.baseId)?' · 本方案已使用':''}</small></span></button>`).join('');
      area.querySelectorAll('[data-ship]').forEach(button=>button.onclick=()=>{team.slots[slot]=button.dataset.ship;close();render();changed();});
    }
    dialog.querySelector('input').oninput=search;search();dialog.querySelector('input').focus();
  }
  function memberHtml(member,slot) {
    const current=state?.inventory.find(cat=>cat.id===member.id);
    const nickname=current?.batchNickname||member.nickname;
    const missing=!current?' · 库存已不存在':'';
    return `<div class="fleet-member"><div class="cat-head-left"><img class="avatar" src="/assets/commander_cats/${esc(member.catId)}.png" onerror="this.src='/assets/commander_cats/placeholder.svg'"><div><h4>${slot} · ${esc(member.id)} ${esc(member.name)}</h4><p>${esc(nickname)} · 天赋 ${esc(member.grade)}${missing}</p></div></div><p><b>本队培养目标：</b>${esc(member.target)}</p>${cultivationPlan(member)}<details><summary>适用依据（天赋分 ${member.talentValue.toFixed(2)}，技能覆盖分 ${member.skillValue.toFixed(2)}）</summary><ul>${member.reasons.map(reason=>`<li>${esc(reason)}</li>`).join('')}</ul></details></div>`;
  }

  function renderSaved(localChange=false) {
    const area=document.querySelector('#fleet-results');if(!area||!data)return;
    const saved=data.recommendations?.[selected];
    if(!saved){area.innerHTML='<p class="note">尚无保存结果；首次载入会自动计算，也可点击推荐。</p>';return;}
    area.innerHTML=(saved.stale||localChange?'<p class="warning">配置已变化，请重新推荐。以下仍是上次保存结果。</p>':'')+
      '<div class="fleet-result-meta"><span>上次推荐：'+esc(localTime(saved.calculatedAt))+'</span><span>已保存 · 手动推荐后更新</span></div><div class="fleet-result-grid">'+
      saved.result.teams.map(team=>'<article class="card"><h3>'+esc(team.name)+'</h3>'+
        (team.complete?memberHtml(team.commander,'司令')+memberHtml(team.staff,'参谋'):'<p class="warning">'+esc(team.warning)+'</p>')+'</article>').join('')+'</div>';
  }
  async function computePlan(id) {
    const response=await requestJson(endpoint+'/recommend',{payload:JSON.stringify({planId:id})});
    data.recommendations=response.recommendations;
    window.loadCatObservations?.();
    renderSaved();
  }
  async function initializeRecommendations() {
    if(initializing||recommending||blocked)return;initializing=true;
    try {
      for(const p of data.document.plans)if(!data.recommendations?.[p.id]){
        if(dirty||saving)break;
        say('首次计算方案：'+p.name+'…');await computePlan(p.id);
      }
      say('已载入保存结果；点击推荐才会重新计算');
    }catch(error){say('首次推荐失败：'+error.message,true);}
    finally{initializing=false;}
  }
  async function recommend() {
    if(recommending||initializing)return say('正在计算，请等待完成');
    recommending=true;
    const button=document.querySelector('#fleet-recommend');
    const controls=[...root().querySelectorAll('button,input,select')];
    const disabled=controls.map(control=>control.disabled);
    try {
      await save();
      if(dirty||saving||blocked)throw new Error('请先完成自动保存；保存失败时可重试');
      controls.forEach(control=>control.disabled=true);
      const plans=data.document.plans.map(p=>({id:p.id,name:p.name}));
      if(!plans.length)throw new Error('请先新增方案');
      button.textContent='正在统一计算所有方案…';
      say('按优先级统一培养方向，全部成功后一起保存…');
      try{await computePlan(plans[0].id);}catch(error){throw new Error(`推荐失败，所有原推荐均保留：${error.message}`);}
      say(`已重新推荐并保存全部 ${plans.length} 个方案`);
    }catch(error){say(error.message,true);}
    finally{controls.forEach((control,i)=>control.disabled=disabled[i]);button.textContent='重新推荐所有方案';recommending=false;}
  }
  async function renderTheoryPlans() {
    const area=document.querySelector('#team-grid');if(!area||!data)return;
    const version=++theoryGeneration;
    area.innerHTML='<p class="note">按你的分队计算全图鉴目标…</p>';
    try{
      const blocks=[];
      for(const p of data.document.plans){
        const result=await requestJson(endpoint+'/theory',{payload:JSON.stringify({planId:p.id})});
        if(version!==theoryGeneration)return;
        blocks.push('<article class="card"><h3>'+esc(p.name)+' · '+(p.mode==='opsi'?'大世界':'主线')+'</h3>'+
          result.teams.map(t=>'<section><h4>'+esc(t.name)+'</h4>'+
            (t.complete?pairBlock('司令',t.commander.catId,t.commander.name,t.commander.target)+pairBlock('参谋',t.staff.catId,t.staff.name,t.staff.target):'<p>'+esc(t.warning)+'</p>')+'</section>').join('')+'</article>');
      }
      area.innerHTML=blocks.join('')||'<p>请先在库存配队里新增方案和舰娘。</p>';
    }catch(error){if(version===theoryGeneration)area.innerHTML='<p class="warning">'+esc(error.message)+'</p>';}
  }
  let exporting=false;
  function exportSelection(){return {fleets:document.querySelector('#fleet-export-plans').checked,inventory:document.querySelector('#fleet-export-inventory').checked};}
  function updateExportButton(){const selected=exportSelection();document.querySelector('#fleet-export').disabled=exporting||!(selected.fleets||selected.inventory);}
  document.querySelector('#fleet-export-plans').onchange=updateExportButton;
  document.querySelector('#fleet-export-inventory').onchange=updateExportButton;
  updateExportButton();
  document.querySelector('#fleet-export').onclick=async()=>{
    const status=document.querySelector('#fleet-export-status');
    const selected=exportSelection();
    if(exporting||!(selected.fleets||selected.inventory))return;
    exporting=true;updateExportButton();
    try{
      if(selected.fleets){
        if(!data)await load();
        await save();if(dirty||saving||blocked)throw new Error('请先完成保存');
      }
      const payload=await requestJson(endpoint+'/export',{payload:JSON.stringify(selected)});
      const download=payload.download;
      const blob=download?new Blob([Uint8Array.from(atob(download.base64),c=>c.charCodeAt(0))],{type:'application/zip'}):new Blob([JSON.stringify(payload,null,2)],{type:'application/json'});
      const url=URL.createObjectURL(blob);
      const link=document.createElement('a');link.href=url;link.download=download?.filename||'mac-fleet-plans.json';link.click();
      setTimeout(()=>URL.revokeObjectURL(url),1000);
      status.textContent='已导出：'+[selected.fleets?'编队与保存推荐':'',selected.inventory?'指挥猫库存':''].filter(Boolean).join('、')+'。'+(selected.fleets&&selected.inventory?'请解压外层ZIP，分别导入库存ZIP和编队JSON。':'');
    }catch(error){status.textContent='导出失败：'+error.message;}
    finally{exporting=false;updateExportButton();}
  };
  window.addEventListener('beforeunload',event=>{if(dirty||saving){event.preventDefault();event.returnValue='';}});
})();
