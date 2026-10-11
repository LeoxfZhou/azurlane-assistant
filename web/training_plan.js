function renderTrainingPlan(plan, talents) {
  if (!plan) return '<p class="note">暂无培养方案</p>';
  const escape = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const dictionary = new Map(talents.map(t => [t.id, t.name]));
  const name = id => escape(dictionary.get(id) || id);
  const names = ids => ids.length ? ids.map(name).join(' + ') : '无';
  const row = (label, value) => `<p class="talents"><b>${label}：</b>${value}</p>`;
  const upgrades = plan.upgradeActions.map(action => `${action.pathTalentIds.map(name).join(' → ')}（共 ${action.requiredPoints} 点）`).join('<br>') || '无';
  return '<p class="note">以下方案以重置后的初始天赋为底稿计算。</p>' + row('最终毕业天赋', names(plan.targetTalentIds)) +
    row('已拥有并保留', names(plan.retainedTalentIds)) +
    (plan.fixedGrowth ? '<p class="note">固定成长：其余天赋随等级解锁；库存只记录初始底稿，不代表当前解锁进度。</p>' : '') +
    row('遇到时优先选择', plan.fixedGrowth ? '固定成长，无需随机学习' : names(plan.acquireTalentIds)) +
    row('需要升级', plan.fixedGrowth ? '随等级成长' : upgrades) +
    row('替换', plan.replacementTalentIds.length ? `天赋满5个后，用后续出现的目标新天赋覆盖：${names(plan.replacementTalentIds)}` : '无需替换现有天赋') +
    row('预计所需后续天赋点', plan.fixedGrowth ? '不适用（固定成长）' : `${plan.requiredPoints} 点`) +
    (plan.flexibleInitialTalentIds.length ? row('机动保留', names(plan.flexibleInitialTalentIds)) : '') +
    (!plan.reachable ? `<p class="warning">不可达：${escape(plan.unreachableReason || '暂无可用路线')}</p>` : '');
}

if (typeof module !== 'undefined') module.exports = {renderTrainingPlan};
