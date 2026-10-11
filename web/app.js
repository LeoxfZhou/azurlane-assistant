let state = null;
let appState = null;
let shipState = null;
let pendingWindowsImport = null;
const $ = selector => document.querySelector(selector);
const esc = value => String(value ?? '').replace(/[&<>"']/g, character => ({
  '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
}[character]));

async function loadCat() {
  const response = await fetch('/api/commander-cats/snapshot');
  if (!response.ok) throw new Error('无法读取本地数据');
  state = await response.json();
  window.loadCatObservations?.();
  $('#status').textContent = `${state.inventory.length} 只库存猫 · ${state.species.length} 只图鉴猫`;
  renderAll();
}

function pairBlock(title, id, name, talents) {
  return `<div class="pair"><img src="/assets/commander_cats/${esc(id)}.png" onerror="this.src='/placeholder.svg'"><div><small>${esc(title)}</small><strong>${esc(name)}</strong><div class="talents">${esc(talents)}</div></div></div>`;
}

function cultivationPlan(value, prefix = '') {
  const structured=prefix==='practical'?value.practicalTrainingPlan:value.trainingPlan;
  if(structured && typeof renderTrainingPlan==='function')return renderTrainingPlan(structured,state.allTalents||[]);
  const key = name => prefix ? `${prefix}${name[0].toUpperCase()}${name.slice(1)}` : name;
  const field = name => value[key(name)] ?? '无';
  const points = value[key('points')] ?? value.points ?? 0;
  return `<div class="cultivation-plan"><p class="talents"><b>已拥有并保留：</b>${esc(field('retained'))}</p><p class="talents"><b>遇到时优先选择：</b>${esc(field('acquire'))}</p><p class="talents"><b>需要升级：</b>${esc(field('upgrades')).replace(/[；;]\s*/g, '<br>')}</p><p class="${field('replacement') === '无' ? 'note' : 'warning'}"><b>替换：</b>${field('replacement') === '无' ? '无需替换现有天赋' : `天赋满5个后，用后续出现的目标新天赋覆盖 ${esc(field('replacement'))}`}</p>${field('flexible') !== '无' ? `<p class="note"><b>非核心机动位：</b>${esc(field('flexible'))}</p>` : ''}<p class="note">预计需要 ${esc(points)} 个后续天赋点；替换不额外消耗点数。</p></div>`;
}

function renderTheory() { window.loadFleetPlans?.(true); }

function renderInventory() {
  const filter = $('#tier-filter').value;
  const cats = state.inventory.filter(cat => !filter || cat.feasibilityTier === filter);
  $('#inventory-grid').innerHTML = cats.length ? cats.map(cat => `<article class="card">
    <div class="cat-head"><div class="cat-head-left"><img class="avatar" src="${esc(cat.image)}" onerror="this.src='/placeholder.svg'"><div><h3>${esc(cat.id)} · ${esc(cat.name)}</h3><div class="meta">${esc(cat.rarity)} · ${esc(cat.faction)}</div></div></div><span class="badge ${cat.fixed ? 'fixed' : cat.feasibilityTier.toLowerCase()}">${cat.fixed ? '固定成长' : `初始天赋 ${esc(cat.feasibilityTier)}`}</span></div>
    <p><b>建议昵称：</b>${esc(cat.batchNickname)}<br><small>实际昵称：${esc(cat.nickname)}（建议不会自动改写）</small></p>
    ${typeof offlineCatHtml==='function'?offlineCatHtml(cat):''}
    <p><b>初始：</b>${esc(cat.initial)}</p><p class="note">${esc(cat.embryoExplanation)}</p>
    <details><summary>查看每种用途与逐天赋依据</summary>${(cat.embryoUses || []).map(use => `<section><h4>${esc(use.name)} · ${esc(use.tier)}</h4><p>X=${use.specialValue}，Y=${use.normalPoints}</p><ul>${use.contributions.map(talent => `<li>${esc(talent.name)}：特殊权重 ${talent.specialValue}；普通有效点 ${talent.normalPoints}</li>`).join('')}</ul></section>`).join('') || '<p>固定成长或资料不足，不做随机天赋评分。</p>'}</details>
    ${cat.protectedRare ? '<span class="badge s">极品保留 · 暂时不用也保留</span>' : ''}
    <p class="note">规则：${esc(cat.ruleVersion)} · 可解释筛选权重，非概率或实际伤害；不受当前编队影响。专项与逐项依据可展开查看，待评不等于淘汰。</p>
    ${cat.category ? `<details><summary>查看已有培养方案参考（不决定天赋等级）</summary><p>${esc(cat.finalRoute)}</p><p><b>目标：</b>${esc(cat.target)}</p>${cultivationPlan(cat)}<p class="note">此方案培养难度参考值：${cat.feasibility.toFixed(1)}；来源排行相对上限：${cat.ceiling == null ? '未量化' : cat.ceiling.toFixed(1)}。不是实际全队增伤。</p>${cat.routesDiffer ? `<h4>另一条实用路线</h4><p>${esc(cat.practicalRoute)}</p><p>${esc(cat.practicalTarget)}</p>${cultivationPlan(cat, 'practical')}` : ''}</details>` : ''}
  </article>`).join('') : '<p>没有符合筛选条件的库存猫。</p>';
}

function renderSettings() {
  $('#data-counts').textContent = `当前载入：${state.stats.species} 只猫、${state.stats.builds} 条培养方案、${state.stats.rankings} 条排行、${state.stats.guides} 条攻略观点。`;
}

function renderAll() {
  renderTheory();
  renderInventory();
  renderSettings();
}

async function requestJson(path, values = {}) {
  const response = await fetch(path, {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded', 'X-Azur-Token': appState?.token || state?.token},
    body: new URLSearchParams(values)
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '操作失败');
  return data;
}

async function mutate(path, values = {}) {
  await requestJson(path, values);
  await loadCat();
}

async function wikiCommand(path) {
  const response = await fetch(path, {method: 'POST', headers: {'X-Azur-Token': appState?.token || state?.token}});
  const data = await response.json();
  if (!response.ok || !data.ok) throw new Error(data.error || 'WIKI 刷新失败');
  return data;
}

$('#tier-filter').onchange = renderInventory;

function bytesToBase64(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = '';
  for (let offset = 0; offset < bytes.length; offset += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  return btoa(binary);
}

function importList(label, values) {
  if (!values.length) return '';
  const shown = values.slice(0, 16).map(esc).join('、');
  return `<li>${esc(label)} ${values.length}：${shown}${values.length > 16 ? '…' : ''}</li>`;
}

function resetWindowsImport(message = '尚未选择文件。支持 commander_cat_inventory.csv 与 commander_cat_settings.csv。') {
  pendingWindowsImport = null;
  const result = $('#windows-import-result');
  result.textContent = message;
  result.classList.remove('success', 'error');
  $('#windows-import-apply').classList.add('hidden');
}

$('#windows-import-files').onchange = () => resetWindowsImport('文件已选择，请先预览；此时不会修改现有数据。');

$('#windows-import-preview').onclick = async () => {
  const files = [...$('#windows-import-files').files];
  const result = $('#windows-import-result');
  if (files.length < 1 || files.length > 2) {
    resetWindowsImport('一次请选择一个或两个文件。');
    result.classList.add('error');
    return;
  }
  if (files.some(file => file.size > 4 * 1024 * 1024)) {
    resetWindowsImport('单个上传文件不能超过 4 MB。');
    result.classList.add('error');
    return;
  }
  const button = $('#windows-import-preview');
  button.disabled = true;
  result.textContent = '正在校验版本、文件校验值和库存关联；Mac 设置不会改变…';
  result.classList.remove('success', 'error');
  $('#windows-import-apply').classList.add('hidden');
  try {
    const values = {file_count: String(files.length)};
    for (let index = 0; index < files.length; ++index) {
      values[`file${index}_name`] = files[index].name;
      values[`file${index}_data`] = bytesToBase64(await files[index].arrayBuffer());
    }
    const preview = await requestJson('/api/commander-cats/import-preview', values);
    pendingWindowsImport = preview;
    const sections = [];
    if (preview.includesInventory) {
      sections.push(`<b>库存：</b>${preview.oldInventoryCount} → ${preview.newInventoryCount} 只<ul>${importList('新增', preview.added)}${importList('移除', preview.removed)}${importList('修改', preview.changed)}${!preview.added.length && !preview.removed.length && !preview.changed.length ? '<li>内容没有变化</li>' : ''}</ul>`);
    } else sections.push('<b>库存：</b>本次不导入，保持现状');
    if (preview.includesSettings) {
      sections.push(`<b>舰队设置：</b>${preview.settingChanges.length ? preview.settingChanges.map(esc).join('；') : '内容没有变化'}`);
    } else sections.push('<b>舰队设置：</b>本次不导入，保持现状');
    result.innerHTML = `${sections.join('<br>')}<p><b>校验已通过。</b>确认后只替换上面列出的数据，并自动保留备份。</p>`;
    result.classList.add('success');
    $('#windows-import-apply').classList.remove('hidden');
  } catch (error) {
    pendingWindowsImport = null;
    result.textContent = `无法导入：${error.message}`;
    result.classList.add('error');
  } finally { button.disabled = false; }
};

$('#windows-import-apply').onclick = async () => {
  if (!pendingWindowsImport) return;
  const button = $('#windows-import-apply');
  const result = $('#windows-import-result');
  if (!confirm('确认用这次 Windows 数据替换预览中列出的 Mac 数据吗？现有文件会先备份。')) return;
  button.disabled = true;
  try {
    const applied = await requestJson('/api/commander-cats/import-apply', {import_id: pendingWindowsImport.importId});
    await loadCat();
    const appResponse = await fetch('/api/app/snapshot');
    if (appResponse.ok) appState = await appResponse.json();
    $('#windows-import-files').value = '';
    pendingWindowsImport = null;
    button.classList.add('hidden');
    result.textContent = `导入完成：${applied.inventoryUpdated ? '库存已更新' : '库存未改动'}，${applied.settingsUpdated ? '舰队设置已更新' : '舰队设置未改动'}。备份已保留。`;
    result.classList.remove('error');
    result.classList.add('success');
  } catch (error) {
    result.textContent = `导入失败：${error.message}。原数据已保留或回滚。`;
    result.classList.add('error');
  } finally { button.disabled = false; }
};

$('#open-windows-import').onclick = () => {
  document.querySelector('#cat-nav [data-view="settings"]').click();
  $('#windows-import-card').scrollIntoView({behavior: 'smooth', block: 'start'});
};


$('#wiki-refresh').onclick = async () => {
  const button = $('#wiki-refresh'), result = $('#wiki-result');
  button.disabled = true;
  button.textContent = '正在下载并比较…';
  result.textContent = '现有头像会保持不变，直到你确认应用。';
  try {
    const preview = await wikiCommand('/api/wiki-preview');
    const details = preview.changed.length ? preview.changed.map(item => `${item.name}：${item.status}`).join('；') : '没有头像变化';
    result.textContent = `已核对 ${preview.downloaded} 张：${details}；${preview.unchanged} 张未变化。`;
    if (confirm(`${result.textContent}\n\n确认应用这次刷新吗？`)) {
      const applied = await wikiCommand('/api/wiki-apply');
      result.textContent = `已应用 ${applied.downloaded} 张本地头像；刷新页面即可重新读取。`;
    }
  } catch (error) {
    result.textContent = `刷新失败：${error.message}。原头像和清单未改变。`;
  } finally {
    button.disabled = false;
    button.textContent = '检查 WIKI 头像更新';
  }
};

document.querySelectorAll('#cat-nav button').forEach(button => button.onclick = () => {
  document.querySelectorAll('#cat-nav button,.cat-view').forEach(element => element.classList.remove('active'));
  button.classList.add('active');
  $('#' + button.dataset.view).classList.add('active');
  if (button.dataset.view === 'pairing' || button.dataset.view === 'theory') window.loadFleetPlans?.();
});

const uniqueSorted = values => [...new Set(values.filter(Boolean))].sort((a, b) => a.localeCompare(b, 'zh-CN'));

async function loadShips() {
  const response = await fetch('/api/ship-stories/snapshot');
  if (!response.ok) throw new Error('无法读取舰船剧情数据');
  shipState = await response.json();
  const camps = uniqueSorted(shipState.ships.map(ship => ship.camp));
  const types = uniqueSorted(shipState.ships.map(ship => ship.type));
  $('#ship-camp').innerHTML = '<option value="">全部阵营</option>' + camps.map(value => `<option>${esc(value)}</option>`).join('');
  $('#ship-type').innerHTML = '<option value="">全部舰种</option>' + types.map(value => `<option>${esc(value)}</option>`).join('');
  renderShips();
}

function shipStatusLabel(value) {
  return {unowned: '未获取', in_progress: '已获取未完成', completed: '剧情已完成'}[value] || value;
}

function renderShips() {
  if (!shipState) return;
  const query = $('#ship-search').value.trim().toLowerCase();
  const values = {
    camp: $('#ship-camp').value, group: $('#ship-group').value,
    type: $('#ship-type').value, max: $('#ship-max').value,
    status: $('#ship-status').value
  };
  const ships = shipState.ships.filter(ship => {
    const names = [ship.name, ship.wikiName, ship.canonicalName, ship.harmonyName, ...ship.aliases].join(' ').toLowerCase();
    return (!query || names.includes(query)) && (!values.camp || ship.camp === values.camp) &&
      (!values.group || ship.group === values.group) && (!values.type || ship.type === values.type) &&
      (!values.max || String(ship.maxLevel) === values.max) && (!values.status || ship.status === values.status);
  });
  $('#ship-stats').textContent = `显示 ${ships.length}/${shipState.stats.total} · 未完成 ${shipState.stats.in_progress} · 已完成 ${shipState.stats.completed}`;
  $('#ship-grid').innerHTML = ships.length ? ships.map(ship => {
    let actions = '';
    if (ship.status === 'unowned') {
      actions = `<button onclick="setShipProgress('${ship.id}','in_progress',false)">加入·未满级</button><button class="secondary" onclick="setShipProgress('${ship.id}','in_progress',true)">加入·已满级</button>`;
    } else if (ship.status === 'in_progress') {
      actions = `<button class="secondary" onclick="setShipProgress('${ship.id}','in_progress',${!ship.maxLevel})">设为${ship.maxLevel ? '未满级' : '已满级'}</button><button onclick="setShipProgress('${ship.id}','completed',true)">标记完成</button><button class="danger" onclick="setShipProgress('${ship.id}','unowned',false)">设为未获取</button>`;
    } else {
      actions = `<button class="secondary" onclick="setShipProgress('${ship.id}','in_progress',true)">恢复为未完成</button><button class="danger" onclick="setShipProgress('${ship.id}','unowned',false)">设为未获取</button>`;
    }
    return `<article class="card ship-card"><div class="catalog-head"><img class="catalog-avatar" src="${esc(ship.image)}" onerror="this.src='/placeholder.svg'"><div><h3>${esc(ship.name)}</h3><div class="meta">${esc(ship.camp)} · ${esc(ship.group)} · ${esc(ship.type)}</div><span class="badge">${esc(shipStatusLabel(ship.status))}${ship.status !== 'unowned' ? ` · ${ship.maxLevel ? '满级' : '未满级'}` : ''}</span></div></div><h4>${esc(ship.storyTitle)}</h4><div class="actions ship-actions">${actions}</div><details><summary>查看 ${ship.steps.length} 章任务与奖励</summary><div class="story-steps">${ship.steps.map(step => `<div><b>${step.order}. ${esc(step.title)}</b><span>${esc(step.requirement)}</span><small>${esc(step.reward)}</small></div>`).join('')}</div></details></article>`;
  }).join('') : '<article class="card"><p>没有符合当前筛选条件的舰娘。</p></article>';
}

async function setShipProgress(shipId, status, maxLevel) {
  try {
    await requestJson('/api/ship-stories/progress', {ship_id: shipId, status, is_max_level: String(maxLevel)});
    await loadShips();
  } catch (error) { alert(error.message); }
}

['#ship-search', '#ship-camp', '#ship-group', '#ship-type', '#ship-max', '#ship-status'].forEach(selector => {
  $(selector).oninput = renderShips;
  $(selector).onchange = renderShips;
});
$('#ship-reset').onclick = () => {
  $('#ship-search').value = ''; $('#ship-camp').value = ''; $('#ship-group').value = '';
  $('#ship-type').value = ''; $('#ship-max').value = ''; $('#ship-status').value = 'in_progress';
  renderShips();
};


const routeInfo = {"azur-lane": ["碧蓝航线助手", "指挥猫、舰船剧情与装备仓库。"], "commander-cats": ["指挥猫作战室", "理论顶配、库存鉴定与配队。"], "ship-stories": ["舰船个人剧情", "管理获取、满级和剧情完成状态。"], "equipment": ["装备仓库", "装备库存与用途分级。"], "app-settings": ["数据设置", "手动检查离线参考资料更新。"]};

function updateRouteHeader(route) {
  const info = routeInfo[route] || routeInfo['azur-lane'];
  $('#app-title').textContent = info[0];
  $('#app-subtitle').textContent = info[1];
  $('#breadcrumbs').innerHTML = `<button data-route="azur-lane">碧蓝航线助手</button><i>›</i><span>${esc(info[0])}</span>`;
  $('#breadcrumbs button').onclick = () => navigate('azur-lane');
}
async function showRoute() {
  const requested = location.hash.replace(/^#\/?/, '') || 'azur-lane';
  const selected = routeInfo[requested] ? requested : 'azur-lane';
  document.querySelectorAll('.page').forEach(page => page.classList.remove('active'));
  $(`#page-${selected}`).classList.add('active');
  updateRouteHeader(selected);
  try {
    if (selected === 'equipment') await window.loadEquipment();
    else if (selected === 'commander-cats' && !state) await loadCat();
    else if (selected === 'ship-stories' && !shipState) await loadShips();
    else $('#status').textContent = `${appState.commanderCats} 只库存猫 · ${appState.ownedStoryShips} 位剧情舰娘`;
  } catch (error) { $('#status').textContent = '加载失败'; alert(error.message); }
}
function navigate(route) { location.hash = `#/${route}`; }
document.querySelectorAll('[data-route]').forEach(button => button.onclick = () => navigate(button.dataset.route));
window.addEventListener('hashchange', showRoute);

document.querySelectorAll('[data-refresh]').forEach(button => button.onclick = async () => {
  const module = button.dataset.refresh, output = $('#data-refresh-result');
  button.disabled = true; output.textContent = '正在下载到暂存区并校验…';
  try {
    const preview = await requestJson('/api/data-refresh/preview', {module});
    const info = preview.modules[module], records = info.recordDiff, assets = info.assetDiff;
    const summary = `资料 ${info.records} 条；记录新增 ${records.added.length}、修改 ${records.modified.length}、删除 ${records.removed.length}、无法关联 ${records.unmatched.length}；图片新增 ${assets.added.length}、修改 ${assets.modified.length}。`;
    const changed = [['新增', records.added], ['修改', records.modified], ['删除', records.removed], ['无法关联', records.unmatched]].filter(([, values]) => values.length).map(([label, values]) => `${label}：${values.slice(0, 12).join('、')}${values.length > 12 ? '…' : ''}`).join('；');
    output.textContent = changed ? `${summary} ${changed}` : `${summary} 没有记录变化。`;
    if (confirm(`${output.textContent}\n\n确认应用吗？个人仓库与进度不会被覆盖；重新双击启动后读取新数据。`)) {
      await requestJson('/api/data-refresh/apply', {module});
      output.textContent = '已原子应用参考数据。请重新启动游戏助手以载入新版本。';
    }
  } catch (error) { output.textContent = `检查失败：${error.message}。旧数据保持不变。`; }
  finally { button.disabled = false; }
});

async function initialize() {
  const response = await fetch('/api/app/snapshot');
  if (!response.ok) throw new Error('无法启动本地游戏助手');
  appState = await response.json();
  await showRoute();
}

initialize().catch(error => { $('#status').textContent = '加载失败'; alert(error.message); });
