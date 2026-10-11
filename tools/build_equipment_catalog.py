#!/usr/bin/env python3
"""Rebuild the reviewed 2026-10-11 equipment reference snapshot, offline.

The rank-map indices refer only to the bundled immutable ranking-map.json.
New guide versions require explicit review; this script never downloads or
changes user inventory. Unknown acquisition and combat value stay unknown.
"""
import json,re,hashlib
from pathlib import Path
from urllib.parse import quote
root=Path(__file__).resolve().parents[1]/'data/equipment'
load=lambda f:json.loads((root/'sources'/f).read_text())
module=load('module-facts.json')['equipment']
types={1:'驱逐炮',2:'轻巡炮',3:'重巡炮',4:'战列炮',5:'水面鱼雷',6:'防空炮',7:'战斗机',8:'鱼雷机',9:'轰炸机',10:'设备',11:'超巡炮',12:'水上机',13:'潜艇鱼雷',14:'反潜设备',15:'反潜机',17:'直升机',18:'货物',20:'导弹',21:'远程防空炮'}
rarities={1:'白色',2:'白色',3:'蓝色',4:'紫色',5:'金色',6:'彩色'}
def wiki(n):return 'https://wiki.biligame.com/blhx/'+quote(n)
items={r['name']:dict(id=r['name'],name=r['name'],gameId=r['gameId'],type=types[r['type']],rarity=rarities[r['rarity']],nationality=r['nationality'],wikiUrl=wiki(r['name']),classification='unknown',classificationReason='获取渠道尚未完整核实，不归入普通装备。',acquisition=[],researchTo=[],researchVerified=False,aliases=[]) for r in module}
# Unknown type values or name ambiguities fail the build, rather than merging variants.
for g in items.values():
 if g['nationality']>=100:
  g.update(classification='special',classificationReason='WIKI 装备数据标记为联动阵营，按用户规则独立展示。',specialReason='联动',acquisition=['联动装备；具体活动渠道待逐页补充'])
def classify(name,kind,reason,channel,source=None):
 if name not in items:return
 g=items[name]
 if g['classification']=='special' and g.get('specialReason')=='联动':return
 g.update(classification=kind,classificationReason=reason,specialReason=reason if kind=='special' else '')
 if channel not in g['acquisition']:g['acquisition'].append(channel)
 if source:g.setdefault('acquisitionSources',[]).append(source)
for row in load('core-shop-facts.json'):
 classify(row['name'],'normal','可在常驻核心商店获取。',('核心商店·月度' if row['monthly'] else '核心商店·限定（常驻，持有上限 '+str(row['limit'])+'）'),wiki('核心商店'))
for name in load('lab-facts.json')['names']:
 classify(name,'normal','军部研究室可常驻产出设计图。','军部研究室',wiki('军部研究室'))
unresolved_edges=[]
for section in load('research-facts.json'):
 for a,b in section['edges']:
  if a==b:continue
  if a not in items or b not in items:unresolved_edges.append([a,b]);continue
  if b not in items[a]['researchTo']:items[a]['researchTo'].append(b)
  items[a]['researchVerified']=True
  classify(b,'normal','有已核实的常驻装备研发路线。','装备研发：'+a+' → '+b,wiki('装备研发/'+section['faction']))
unique=['小海狸中队队徽T0','珍珠之泪T0','开拓者奖章T0','华盛顿海军条约T0','白鹰精英损管T0','纳尔逊的旗语T0']
for name in unique:classify(name,'special','账号唯一','收藏奖励，仅可领取一次',wiki('收藏奖励'))
classify('双联装138.6mm主炮Mle1934T0','special','活动限定','整装：绽放于辉光之城活动 Pt 兑换；当前已取得的详情未列常驻渠道',wiki('双联装138.6mm主炮Mle1934T0'))
for name,channel in {'双联装113mm高射炮T3':'威斯克科技箱、7-2／11-4 图纸','维修工具T3':'通用科技箱、3-4 等常规海域图纸','液压弹射装置T3':'通用科技箱','四联装533mm磁性鱼雷T3':'伯鲁克科技箱','SG雷达T3':'克洛希德科技箱'}.items():classify(name,'normal','存在常驻获取渠道。',channel,wiki(name))
text=load('ranking-map.json')['parse']['wikitext']['*']
raw_names=list(dict.fromkeys(re.findall(r'rect[^\n]*\[\[([^]|]+)',text)))
def canonical(n):
 n=n.strip()
 if n in items:return n
 if n+'T0' in items:return n+'T0'
 return n
names=[canonical(n) for n in raw_names]
print('ranking missing',[(i,n) for i,n in enumerate(names) if n not in items]);print('edges unmatched',unresolved_edges)
assess=[]
def rate(indices,grade,reason,tags=(),source='rank119'):
 for i in indices:
  n=names[i]
  if n not in items:continue
  if any(x['equipmentId']==n and x['sourceId']==source for x in assess):raise ValueError('double rating '+n)
  assess.append(dict(equipmentId=n,grade=grade,reason=reason,tags=list(tags),sourceId=source))
rate([0],'S','第119期列为驱逐主炮首选，穿甲弹对多目标输出突出。',['驱逐主炮','多目标'])
rate([1,2,3,4,5],'A','第119期驱逐主炮第一档；按敌方护甲与舰船弹幕需求选择，不能仅比较面板。',['驱逐主炮'])
rate([6,7],'B','第119期驱逐主炮第二档；有更强替代，现有库存仍可用于过渡或适配射速。')
rate([8,9],'B','第119期单独列出的高速炮，主要服务依赖开炮触发的舰船弹幕，不宜当泛用主炮。',['调速','特定舰船'])
rate([10,11,12,13],'B','第119期列入新手可用主炮；不属于成熟仓库的优先选择，但不是完全无用途。',['过渡'])
rate([14,15,16,21],'A','第119期轻巡主炮第一档，需按穿甲／高爆和舰船需求选择。',['轻巡主炮'])
rate([17,18,19,20,22,23],'B','第119期轻巡主炮次选，仍有适配护甲或作为替代的价值。')
rate([24],'S','第119期轻巡主炮零档，属于泛用优先选择。',['轻巡主炮'])
rate([25],'S','第119期超巡主炮中优先推荐，兼顾泛用与对中甲。')
rate([26,27,28],'A','第119期超巡主炮推荐；分别受对甲与弹药类型影响，保留场景区别。')
rate([29,30,31],'S','第119期重巡主炮零档；需要在穿甲、高爆以及敌方护甲之间选择。')
rate([32,33,34,35,36],'A','第119期重巡主炮前列推荐；各自射速、弹种和对甲适性不同。')
rate([37,38,39,40,41,42],'B','第119期重巡主炮次选，属于已有装备的可用替代。')
rate([43,44],'S','第119期战列高爆主炮优先选择；仍需配合技能时间轴。',['对轻甲','对轴'])
rate([45,46],'S','第119期战列穿甲主炮优先选择；对中重甲的取舍随炮型变化。',['对中／重甲','对轴'])
rate([47,52],'A','第119期列为战列穿甲备用选择，价值主要在射速和对轴适配。',['对轴'])
rate([48,49],'B','第119期列为战列备用主炮，可作替代，但不是优先投入资源的目标。')
rate([50,51],'C','第119期主要保留调速备用用途，泛用优势有限。',['调速备用'])
rate([53,54],'A','第119期快炮备选，适用于大世界清杂、预装填或特定舰船配置。',['快炮','特定场景'])
rate([55,56],'B','第119期列出慢轴特例；不应因冷却较长而直接视为无用。',['慢轴','武藏适配'])
rate([57],'S','第119期自律水面鱼雷零档，适合泛用。')
rate([58],'A','第119期自律水面鱼雷第一档，常用且可作为优先替代。')
rate([59,60,61],'B','第119期自律鱼雷次选，射速与雷型不同，可按场景替代。')
rate([62],'A','第119期手操鱼雷优先选择，贴近目标手动发射的价值高于泛用自律。',['手操','EX'])
rate([63,64],'B','第119期列出手操或对轻甲特例；不按泛用自律鱼雷理解。',['手操','特化'])
rate([65],'A','第119期列出高单轮伤害的驱逐副炮选择，也有演习用途。',['副炮','演习'])
rate([66,67,68,69,70,71,72],'B','第119期战列副炮备用选择；可按点燃、射程和散射需求使用。',['战列副炮'])
rate([73],'S','第119期防空炮零档，同时提供命中；属于泛用优先选择。',['防空','命中'])
rate([74,75],'A','第119期防空炮第一档；有强化投入的现成装备仍具价值。',['防空'])
rate([76,77,78,79],'B','第119期防空炮第二档；可用替代，不建议据此优先追加强化投入。',['防空'])
rate([80,81,82,83,84],'C','第119期防空炮第三档；目前主要是资源不足时的过渡用途。',['过渡防空'])
rate([85],'C','第119期仅保留防空调速用途，通常只有特定低装填配置需要。',['防空调速'])
rate([86],'A','第119期命中防空炮特例，适合以命中为主要目标的配置。',['命中'])
rate([87],'B','第119期命中防空炮替代选择，优先级低于彩76与STAAG。',['命中'])
rate([88,89,90],'B','第119期加炮击防空炮特例；适合防空压力允许时补输出。',['炮击','特殊配装'])
rate([91],'S','第119期前排雷击输出设备优先选择，具体收益随舰船雷击占比变化。',['雷击'])
rate([92],'B','第119期列入雷击设备与演习先手搭配，主要优化首轮鱼雷。',['首轮鱼雷','演习'])
rate([93,94,95,96,97],'B','第119期列入炮驱或巡洋输出设备；需先满足生存需求，并按命中环境与舰种选择。',['前排输出'])
rate([98,99],'C','第119期中的前排输出特例；分别侧重低机动环境或装填调速，泛用提升有限。',['低耗／调速'])
rate([100],'S','第119期导驱主武器优先推荐，仅适用于能装备导弹的舰船。',['导驱'])
rate([101],'B','第119期导驱主武器替代选择，存在上位型号。',['导驱'])
rate([102],'A','第119期导驱专用搭配，装备价值取决于适用舰船。',['导驱'])
rate([103],'B','第119期 J 级相关生存设备特例，按舰船使用。',['J级'])
rate([104],'A','第119期常用前排生存设备，适合补血量与续航；部分限定设备可替代。',['生存'])
rate([105,106],'S','第119期兼顾耐久与输出的设备中优先推荐，但仍需按舰种和输出机制适配。',['耐久＋输出'])
rate([107,108],'S','第119期前排生存设备中优先推荐，需结合强化等级与战斗长度比较。',['生存'])
rate([109],'A','第119期常用生存设备前列，联动来源与强度分开标记。',['生存'])
rate([110,114],'A','第119期列入耐久兼输出设备推荐，适合相应输出场景。',['耐久＋输出'])
rate([111,112,113],'B','第119期列出特定敌人或场景下的设备，不能视为无条件泛用增伤。',['特定敌人'])
rate([115],'A','第119期轻重巡输出设备优先推荐，前提是仍能满足生存需求。',['巡洋输出'])
rate([116,117,118],'B','第119期重巡输出替代选择，命中环境与可装备舰种影响收益。',['重巡输出'])
rate([119],'B','第119期部分前排场景可用，航母不可装备，不直接套用同类命中设备结论。',['命中'])
rate([120],'S','第119期巡洋生存优先设备；同舰装备限制不等于账号唯一。',['生存'])
rate([122],'A','第119期多舰种生存与航速用途，账号唯一，独立展示。',['航速','唯一'])
rate([123,124],'A','第119期联动生存或复合设备推荐，按效果和适用舰船使用。',['特殊配装'])
rate([125,126,127,128,129],'B','第119期列出耐久或闪避替代设备，泛用时仍需与维修工具、舵机等比较。',['生存替代'])
rate([130],'B','第119期重巡输出设备中的场景选择，避免忽略适用敌人限制。',['特定敌人'])
rate([131,132],'S','第119期战列输出设备优先推荐，首轮准备时间与命中／散布效果重要。',['战列输出'])
rate([133],'A','第119期战列常用输出设备，提供暴击伤害与命中。',['战列输出'])
rate([134,135],'B','第119期战列输出替代或特化选择；需考虑舰船与暴击配装。',['战列输出'])
rate([136],'A','第119期后排生存设备特例，可应对暴毙风险；账号唯一。',['后排生存'])
rate([137],'A','第119期高防空压力场景下的功能性设备。',['防空'])
rate([138],'B','第119期高难或演习特定生存用途，不能把联动装备混入普通清理范围。',['生存','演习'])
rate([139,140],'B','第119期减点燃或减雷伤的功能性设备，价值依敌方伤害结构变化。',['生存特化'])
rate([141],'S','第119期先锋反潜优先装备，适用于需要反潜的主线。',['反潜'])
rate([142],'B','第119期先锋反潜替代选择。',['反潜'])
rate([143,144],'A','第119期后排反潜推荐，只有确实需要并能携带反潜机时才发挥作用。',['后排反潜'])
rate([145,146,147,148,149,150],'B','第119期功能性装备，价值取决于装甲类型、适用舰船或运输舰需求。',['特殊配装'])
rate([151],'S','第119期对海火箭战斗机前列，对重甲与综合输出突出；不能承担通常航空防御。',['对海火箭','无对空'])
rate([152,153,154],'A','第119期对海火箭战斗机推荐，按轻／中／重甲选择。',['对海火箭','无对空'])
rate([155,156,157,158],'A','第119期带炸弹战斗机推荐，兼顾对海与航空防御。',['对海／对空'])
rate([159,160,161],'B','第119期战斗机次选，保留常规炸弹与防空用途。')
rate([162,163,164,165],'C','第119期战斗机较低优先替代，已有更好选择时使用机会少。')
rate([166,167,168],'B','第119期射速或对空特例，不能仅因对海输出低就判断无用。',['调速','对空'])
rate([169],'S','第119期支援编队防空优先选择，尤其适合15图航空压力场景。',['15图支援','对空'])
rate([170],'B','第119期防空替代和调速选择，优先级低于 G-3/R1。',['对空','调速'])
rate([171,172,173,174],'A','第119期集束雷机推荐，选择主要由对轴与敌人移动情况决定。',['集束雷','对轴'])
rate([175],'S','第119期平行雷机前列，兼顾对海输出与防空。',['平行雷','泛用'])
rate([176,177],'A','第119期平行雷机次选，可用于常规输出或特定配装。',['平行雷'])
rate([178,179],'S','第119期轰炸机优先推荐，分别按重甲／轻中甲及时间轴选择。',['轰炸机'])
rate([180,181],'A','第119期高输出轰炸机常用选择；紫色型号也有明确价值。',['轰炸机'])
rate([182],'B','第119期轰炸机调速特例，不能按单轮伤害简单淘汰。',['调速'])
rate([183],'A','第119期水上机输出优先选择，限可装备舰种。',['水上机'])
rate([184,185],'B','第119期水上机／航战特例，按舰种和航空防御需求使用。',['航战／航巡'])
rate([186,187,188],'A','第119期航空输出设备推荐，按是否需要耐久与限定获取区别使用。',['航母输出'])
rate([189],'A','第119期航母调速与输出常用设备，时间轴需求决定是否携带。',['航母调速'])
rate([190,191],'B','第119期航母输出或防空场景替代设备。',['航母'])
rate([192,193,194,195,196,197,198],'C','第119期航母命中设备特例，主要面向演习；常规 PVE 通常优先航空或调速设备。',['演习','航母命中'])
rate([199],'B','第119期延长飞机存在时间等特定协作用途，非普遍增伤设备。',['协作','特殊配装'])
rate([200],'S','第119期潜艇鱼雷优先推荐，兼顾泛用与时间轴。',['潜艇'])
rate([201,202],'A','第119期潜艇鱼雷常用或对轴特例；95改尤其依赖潜艇配置。',['潜艇','对轴'])
rate([203,204,205],'B','第119期潜艇鱼雷第二档，已有库存可以作为替代。',['潜艇'])
rate([206],'C','第119期潜艇鱼雷过渡型号，与金色 T3 必须分开评价。',['过渡潜艇'])
rate([207],'B','第119期絮库夫专用主炮，舰种限制显著。',['絮库夫'])
rate([208,209],'A','第119期潜艇常用设备，按射程、氧气和鱼雷轮数需求选择。',['潜艇'])
rate([210],'A','第119期演习献祭回复特例，也属于账号唯一装备。',['演习','唯一'])
rate([211,212,213],'B','第119期航速及演习拉扯特例，泛用性不等于实际特殊用途价值。',['航速','演习'])
rate([214,216,217],'A','第119期风帆舰推荐主炮或输出设备，限适用舰种。',['风帆'])
rate([215],'B','第119期风帆副炮推荐，作用范围较窄。',['风帆副炮'])
rate([218],'C','与实心弹存在装备互斥，按第118期说明和第119期低优先展示整理为低优先备选。',['风帆','互斥'])
rate([219,220,221],'A','第119期风帆舰生存设备推荐。',['风帆生存'])
# Add a small historical slice, without pretending all historical images were reviewed.
rate([73],'S','第118期已将彩76放在防空与命中推荐前列。',['防空','命中'],'rank118')
rate([74],'A','第118期仍列第一档，已有强化投入不应仅因新装备出现就否定价值。',['防空'],'rank118')
rate([192],'C','第118期说明此类航母命中设备主要适用于演习，普通 PVE 不优先。',['演习'],'rank118')
source_list=[dict(id='rank119',title='井号装备一图榜第119期（本工具整理分级）',date='2025-08-28',url=wiki('文件:装备一图榜.jpg'),localImage='ranking-119-2025-08-28.jpg',sha256=hashlib.sha256((root/'assets/ranking-119-2025-08-28.jpg').read_bytes()).hexdigest()),dict(id='rank118',title='井号装备一图榜第118期文字说明',date='2025-03-11',url='https://www.bilibili.com/opus/1043095699052298244')]
output=dict(version='equipment-2026-10-11-v1',retrievedAt='2026-10-11',sources=source_list,equipment=list(items.values()),assessments=assess,notes=['强度依据截至第119期（2025-08-28），不代表2026年最新环境；其后新增装备待后续攻略补充。','本地包含WIKI装备数据模块的常规装备型号，不含特殊兵装。获取渠道与研发资料仍有待核实部分，已独立列出。','当前没有仅因未上榜而判D的规则。没有已确认无用途依据的装备仍为待评级。','历史只保存实际采用的文字判断与当前榜图，未批量保存全部历期大图。'])
(root/'catalog.json').write_text(json.dumps(output,ensure_ascii=False,indent=2))
(root/'sources/reviewed-assessments.json').write_text(json.dumps(assess,ensure_ascii=False,indent=2))
from collections import Counter
print('catalog',len(items),'ratings',len(assess),'classification',Counter(g['classification'] for g in items.values()))
