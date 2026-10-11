let catObservations = null;
async function loadCatObservations() {
  try {
    const response = await fetch('/api/commander-cats/offline');
    if (!response.ok) throw Error('观测记录读取失败');
    catObservations = await response.json();
  } catch (error) { catObservations = {error: error.message}; }
  if(state)renderInventory();
}
function offlineCatHtml(cat) {
  if (!catObservations) return '<p class="note">正在读取离线培养观测…</p>';
  if(catObservations.error)return `<p class="warning">${esc(catObservations.error)}</p>`;
  const entry=catObservations.cats?.[cat.id];
  if(!entry)return '';
  const names=ids=>(ids||[]).map(id=>state.allTalents?.find(t=>t.id===id)?.name||id).map(esc).join(' + ')||'无';
  const safe=cat.batchNickname.replace('装B','装乙');
  const completed=safe.endsWith('成')?safe:safe+'成';
  return `<details><summary>培养观测：${esc(entry.label)}</summary><p class="note">${esc(catObservations.notice)}</p>
    <p><b>观测到的天赋：</b>${names(entry.current)}</p><p><b>历史培养目标：</b>${names(entry.historicalTarget)}</p>
    ${entry.completedTarget?.length?`<p><b>历史完成目标：</b>${names(entry.completedTarget)}</p>`:''}
    ${entry.appendSuffix?`<p><b>毕业昵称建议：</b>${esc([...completed].length<=8?completed:'昵称超过8字，请在Windows调整')}（不改写实际昵称）</p>`:''}
    <div class="observation-images">${(entry.images||[]).map(url=>`<a href="${esc(url)}" target="_blank" rel="noopener"><img loading="lazy" src="${esc(url)}" alt="Windows历史培养截图"></a>`).join('')}</div></details>`;
}

let fleetImportPreview=null;
document.querySelector('#fleet-import-preview').onclick=async()=>{
  const status=document.querySelector('#fleet-import-status');
  document.querySelector('#fleet-import-apply').disabled=true;fleetImportPreview=null;
  try{
    const file=document.querySelector('#fleet-import-file').files[0];
    if(!file)throw Error('请选择Windows导出的编队JSON');
    if(file.size>1024*1024)throw Error('文件超过1MB');
    const transfer=JSON.parse(await file.text());
    fleetImportPreview=await requestJson('/api/commander-cats/fleets/import-preview',{payload:JSON.stringify({transfer})});
    status.textContent=`方案数：${fleetImportPreview.oldPlans} → ${fleetImportPreview.newPlans}。${fleetImportPreview.message}`;
    document.querySelector('#fleet-import-apply').disabled=false;
  }catch(error){status.textContent=error.message;}
};
document.querySelector('#fleet-import-file').onchange=()=>{fleetImportPreview=null;document.querySelector('#fleet-import-apply').disabled=true;};
document.querySelector('#fleet-import-apply').onclick=async()=>{
  if(!fleetImportPreview||!confirm('确认替换编队和历史推荐？原数据会备份，库存不会修改。'))return;
  const button=document.querySelector('#fleet-import-apply');button.disabled=true;
  try{
    await requestJson('/api/commander-cats/fleets/import-apply',{payload:JSON.stringify({previewId:fleetImportPreview.previewId})});
    fleetImportPreview=null;document.querySelector('#fleet-import-status').textContent='已导入；请重新推荐所有方案。';
    await window.loadFleetPlans?.(true);await loadCatObservations();
  }catch(error){document.querySelector('#fleet-import-status').textContent=error.message;}
};
