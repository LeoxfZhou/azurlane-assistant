#include "commander_cat_assessment.hpp"
#include "commander_cat_database.hpp"
#include "commander_cat_inventory.hpp"
#include "commander_cat_settings.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#ifndef AZUR_LANE_COMMANDER_CAT_DATA_DIR
#error "测试目标必须指定指挥猫数据目录"
#endif

namespace {

void expect(bool condition,const std::string& message){if(!condition)throw std::runtime_error("测试失败："+message);}

class TemporaryDirectory{
public:TemporaryDirectory(){path_=std::filesystem::temp_directory_path()/("azurlane_cat_tests_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(path_);}~TemporaryDirectory(){std::error_code ignored;std::filesystem::remove_all(path_,ignored);}const std::filesystem::path& path()const{return path_;}
private:std::filesystem::path path_;
};

std::vector<std::string> cheapestTargets(const CommanderCatDatabase& db,const std::string& catId,const std::string& category){for(const auto* plan:db.buildsFor(catId))if(plan->category==category){std::vector<std::string> result;for(const auto& group:plan->targetTalentGroups)if(!group.empty())result.push_back(group.front());return result;}return{};}

void testDatabase(const CommanderCatDatabase& db){
    expect(db.species().size()==34,"应加载完整 34 只指挥猫");
    expect(db.builds().size()==133,"应保留 133 个图片培养方案");
    expect(db.rankings().size()==122,"应保留 122 条图片排行");
    std::map<std::string,int> counts;for(const auto& rank:db.rankings())++counts[rank.category];
    expect(counts["战列"]==28&&counts["潜艇"]==17&&counts["重巡"]==8&&counts["轻巡"]==15&&counts["驱逐"]==24&&counts["突击者"]==10&&counts["航母"]==20,"七类排行数量应与图片一致");
    expect(db.requireSpecies("lime").faction=="皇家","约翰喵阵营应校正为皇家");
    expect(db.requireSpecies("drake").faction=="飓风","官方阵营名应为飓风");
    expect(db.findSpecies("风帆").size()==7,"旧称风帆应作为飓风阵营输入别名");
    expect(db.findTalentByName("炮术新兵·主力")->name=="炮击新手·主力","OCR 天赋名应仅作为别名");
    expect(db.findTalentByName("熟练指挥官·空母")->name=="精锐指挥官·空母","空母 OCR 名应映射规范名称");
    expect(db.initialPoolFor("oscar").count("special_one_shot")==1,"林德喵初始池应包含一发入魂");
    expect(db.guideRecommendations().size()==20,"应加载 Excel 攻略转换出的 20 条观点记录");
    expect(!db.nicknameAllowed("战参伤C4")&&db.nicknameAllowed("战参伤C5"),"昵称生成应跳过 C4 屏蔽片段");
    const auto route=db.fixedRouteFor("drake");expect(route.size()==7&&route.front()->level==0&&route.back()->level==30,"固定猫应有初始及 5—30 级完整路线");
    int fixedCats=0;for(const auto& cat:db.species())if(cat.fixedRoute)++fixedCats;expect(fixedCats==7,"应有 7 只固定成长猫");
}

void testAssessment(const CommanderCatDatabase& db){
    CatAssessor assessor(db);
    const auto contains=[](const std::vector<std::string>& values,const std::string& id){return std::find(values.begin(),values.end(),id)!=values.end();};
    InventoryCat good{"CAT0001","oscar","",{"special_one_shot","special_fire","gunnery_main_2"},std::nullopt,{}};
    const auto result=assessor.assess(good);expect(result.mainRoute.has_value(),"优质林德喵应有可达主路线");const auto& main=result.routes[*result.mainRoute];expect(main.reachable&&main.requiredPoints<=6,"主路线必须在六点内可达");expect(main.hasRanking&&main.strengthTier!="U","图片排行猫应获得量化档位");expect(!assessor.automaticNickname(main).empty(),"应生成自动短名");expect(result.alternatives.size()<=2,"替代方案最多两个");
    InventoryCat exactSix{"CAT0004","oscar","",{"special_one_shot","special_fire","gunnery_main_1"},std::nullopt,{}};const auto sixResult=assessor.assess(exactSix);expect(std::any_of(sixResult.routes.begin(),sixResult.routes.end(),[](const auto& route){return route.reachable&&route.requiredPoints==6;}),"刚好需要六点的路线仍应可达");
    InventoryCat bad{"CAT0002","oscar","",{"helm_medium_1","aa_vanguard_1","engine_destroyer_1"},std::nullopt,{}};const auto badResult=assessor.assess(bad);expect(std::any_of(badResult.routes.begin(),badResult.routes.end(),[](const auto& route){return !route.reachable&&route.requiredPoints>6;}),"无关初始天赋应产生超过六点的不可达路线");
    InventoryCat fixed{"CAT0003","kidd","",db.fixedRouteFor("kidd").front()->talentIds,std::nullopt,{}};const auto fixedResult=assessor.assess(fixed);expect(fixedResult.fixedRoute&&fixedResult.mainRoute&&fixedResult.routes[*fixedResult.mainRoute].strengthTier=="U","缺少量化排行的固定猫应走固定路线并保持 U 级");
    const auto goodPotential=assessor.assessPotential(good);expect(goodPotential.quantified&&goodPotential.score>=0.0&&goodPotential.score<=100.0&&goodPotential.ceilingQuantified,"有排行的猫应同时计算顶配可行度和毕业上限");expect(goodPotential.route.buildId==goodPotential.finalRoute.buildId&&goodPotential.score==goodPotential.finalRoute.feasibilityScore&&goodPotential.tier=="S"&&goodPotential.nearGraduationStart==goodPotential.finalRoute.nearGraduationStart,"主路线、主评分、档位和准毕业标签必须统一来自最终顶配路线");const auto badPotential=assessor.assessPotential(bad);expect(!badPotential.route.reachable&&!badPotential.tier.empty()&&badPotential.score==0.0,"不可达最终顶配路线的顶配可行度应为零");expect(goodPotential.score>badPotential.score,"高质量彩天赋开局应明显优于无关普通天赋");const auto fixedPotential=assessor.assessPotential(fixed);expect(fixedPotential.fixedGrowth&&fixedPotential.tier=="固定"&&!fixedPotential.ceilingQuantified&&fixedPotential.ceilingTier=="U","固定猫应显示固定成长且量化上限为 U");expect(!assessor.nicknameBase(goodPotential).empty(),"双评分结果应生成昵称基础");
    bool foundDifferentRoutes=false;for(const auto& species:db.species()){if(species.fixedRoute)continue;for(const auto* build:db.buildsFor(species.catId)){std::vector<std::string> initial;for(const auto& group:build->targetTalentGroups)if(!group.empty()&&initial.size()<3)initial.push_back(group.front());const auto potential=assessor.assessPotential({"DIFF",species.catId,"",initial,std::nullopt,{}});if(!potential.routesDiffer)continue;foundDifferentRoutes=true;expect(potential.route.buildId==potential.finalRoute.buildId&&potential.score==potential.finalRoute.feasibilityScore&&!potential.tier.empty(),"最终路线与实用路线不同时，主评分仍必须来自最终路线");expect(!potential.practicalRoute.buildId.empty()&&potential.practicalRoute.feasibilityScore>=0.0,"实用路线应保留独立可行度和培养信息");break;}if(foundDifferentRoutes)break;}expect(foundDifferentRoutes,"测试数据中应存在最终路线与实用路线不同的鉴定样例");
    const auto newStarPotential=assessor.assessPotential({"UNRELATED1","oscar","",{"special_new_star","special_one_shot","gunnery_main_2"},std::nullopt,{}});const auto bestFriendPotential=assessor.assessPotential({"UNRELATED2","oscar","",{"special_best_friend","special_one_shot","gunnery_main_2"},std::nullopt,{}});expect(std::find(newStarPotential.finalRoute.targetTalentIds.begin(),newStarPotential.finalRoute.targetTalentIds.end(),"special_new_star")==newStarPotential.finalRoute.targetTalentIds.end()&&std::find(bestFriendPotential.finalRoute.targetTalentIds.begin(),bestFriendPotential.finalRoute.targetTalentIds.end(),"special_best_friend")==bestFriendPotential.finalRoute.targetTalentIds.end(),"当前最终顶配路线不应要求被期待的新星或最佳玩伴");expect(newStarPotential.score==bestFriendPotential.score,"两个与最终顶配无关的特殊天赋不应产生不同的附加缺失扣分");
    RankingRecord steelPlan;steelPlan.category="潜艇";steelPlan.targetTalentGroups={{"special_wolf_pack"},{"special_fire"},{"special_wind"},{"command_submarine_3"},{"torpedo_submarine_2"}};const auto steel=assessor.assessRankingRoute({"CAT0002","steel","",{"command_submarine_1","helm_small_1","special_fire"},std::nullopt,{}},steelPlan);
    expect(steel.reachable&&steel.requiredPoints==6,"CAT0002 克雷喵应能用六点完成当前最终路线");
    expect(contains(steel.targetTalentIds,"special_fire")&&contains(steel.retainedTalentIds,"special_fire"),"已有侵略如火必须保留在最终毕业天赋中");
    expect(!contains(steel.priorityTalentIds,"special_fire")&&!contains(steel.priorityTalentIds,"helm_small_1"),"已拥有的侵略如火和待替换废天赋都不应混入后续优先");
    expect(contains(steel.acquireTalentIds,"special_wolf_pack")&&contains(steel.acquireTalentIds,"special_wind")&&contains(steel.acquireTalentIds,"torpedo_submarine_1"),"CAT0002 应明确列出三个待获取目标成长链");
    expect(steel.overwriteCount==1&&steel.replacementTalentIds.size()==1&&steel.replacementTalentIds.front()=="helm_small_1","CAT0002 五槽后应覆盖操舵手·小型舰");
    expect(std::any_of(steel.upgradeActions.begin(),steel.upgradeActions.end(),[](const auto& action){return action.pathTalentIds==std::vector<std::string>{"command_submarine_1","command_submarine_2","command_submarine_3"}&&action.requiredPoints==2;}),"潜艇指挥成长链应显示完整两级升级路径");
    expect(std::any_of(steel.upgradeActions.begin(),steel.upgradeActions.end(),[](const auto& action){return action.pathTalentIds==std::vector<std::string>{"torpedo_submarine_1","torpedo_submarine_2"}&&action.requiredPoints==1;}),"新学潜艇雷击后应显示升至熟练的路径");
    const auto oscarPotential=assessor.assessPotential({"CAT0050","oscar","",{"command_battleship_2","command_cruiser_1","gunnery_main_1"},std::nullopt,{}});const auto& oscar=oscarPotential.finalRoute;
    expect(contains(oscar.unrelatedInitialTalentIds,"command_cruiser_1")&&contains(oscar.replacementTalentIds,"command_cruiser_1"),"CAT0050 的新晋指挥官·巡洋应作为五槽后的覆盖目标");
    expect(!contains(oscar.priorityTalentIds,"command_cruiser_1"),"CAT0050 的待覆盖天赋不得显示成培养优先项");
    RankingRecord forbidden;forbidden.targetTalentGroups={{"command_faction_royal_1"}};
    expect(!assessor.assessRankingRoute(good,forbidden).reachable,"不能学习异阵营指挥天赋");
    forbidden.targetTalentGroups={{"fixed_battle_readiness"}};
    expect(!assessor.assessRankingRoute(good,forbidden).reachable,"普通猫不能学习固定猫专属天赋");
    RankingRecord placeholders;placeholders.targetTalentGroups={{"special_fire"},{"random_1"},{"random_2"},{"random_3"}};
    expect(assessor.assessRankingRoute(good,placeholders).targetTalentIds==std::vector<std::string>{"special_fire"},"随机天赋不占明确目标槽");
    RankingRecord duplicate;duplicate.targetTalentGroups={{"gunnery_main_1"},{"gunnery_main_3"},{"special_fire"},{"special_fire"}};
    const auto dedup=assessor.assessRankingRoute({"DUP","oscar","",{"gunnery_main_2","gunnery_main_1","special_fire"},{},{}},duplicate);
    expect(dedup.requiredPoints==1&&dedup.targetTalentIds.size()==2&&dedup.acquireTalentIds.empty(),"重复成长链只按最高等级计点一次");
    expect(dedup.retainedTalentIds.size()==2&&dedup.upgradeActions.size()==1,"重复输入不能制造多个槽位和升级动作");
    RankingRecord flexible;flexible.targetTalentGroups={{"special_fire"}};
    const auto spare=assessor.assessRankingRoute(good,flexible);
    expect(spare.overwriteCount==0&&spare.flexibleInitialTalentIds.size()==2,"未满五槽保留机动天赋");
    RankingRecord crowded;crowded.targetTalentGroups={{"special_fire"},{"special_wind"},{"special_fate"},{"special_forest"},{"special_mountain"},{"special_miracle"}};
    expect(!assessor.assessRankingRoute(good,crowded).reachable,"六个目标成长链不能毕业");
    expect(fixedPotential.route.retainedTalentIds==fixed.initialTalentIds&&fixedPotential.route.acquireTalentIds.empty(),"固定成长不可冒充当前已拥有全部毕业天赋");
    // Every catalog route must obey the same accounting identities.
    for(const auto& species:db.species())if(!species.fixedRoute){
        InventoryCat probe{"AUDIT",species.catId,"",{"special_fire","helm_small_1","gunnery_main_2"},{},{}};
        for(const auto& r:assessor.assess(probe).routes){
            std::set<std::string> families;
            for(const auto& id:r.targetTalentIds){const auto& t=db.requireTalent(id);expect(t.kind!=TalentKind::Placeholder&&families.insert(t.familyId).second,"全路线目标必须去重且排除占位符");}
            int points=static_cast<int>(r.acquireTalentIds.size());for(const auto& action:r.upgradeActions)points+=action.requiredPoints;
            expect(points==r.requiredPoints,"全路线学习加升级等于总点数，替换不额外计点");
            expect(r.replacementTalentIds.size()==static_cast<std::size_t>(std::min(r.overwriteCount,static_cast<int>(r.unrelatedInitialTalentIds.size()))),"替换数量一致");
            if(r.reachable)expect(r.requiredPoints<=6&&families.size()<=5,"所有可达路线均受五槽六点约束");
        }
    }
    InventoryCat cTierCat{"CAT0100","oscar","",{"special_one_shot","special_fire","gunnery_main_1"},std::nullopt,{}};auto cTierPotential=assessor.assessPotential(cTierCat);cTierPotential.tier="C"; // Isolate nickname collision policy from grading.
    const auto blockedBase=assessor.nicknameBase(cTierPotential);std::vector<InventoryCat> named={{"CAT0101","oscar",blockedBase+"1",{},std::nullopt,{}},{"CAT0102","oscar",blockedBase+"2",{},std::nullopt,{}},{"CAT0103","oscar",blockedBase+"3",{},std::nullopt,{}}};const auto safeName=assessor.uniqueNickname(cTierPotential,named);expect(safeName==blockedBase+"5"&&safeName.find("C4")==std::string::npos,"自动昵称应保证唯一并跳过 C4 屏蔽片段");
}

void testInventory(){
    TemporaryDirectory temp;const auto path=temp.path()/"inventory.csv";CommanderCatInventory inventory(path);inventory.load();auto first=inventory.add("oscar","炮参纯伤S",{"special_one_shot","gunnery_main_2","special_fire"});expect(first.instanceId=="CAT0001","首个编号应为 CAT0001");auto second=inventory.add("oscar","备用",{"special_one_shot","gunnery_main_2","special_fire"});expect(second.instanceId=="CAT0002"&&inventory.cats().size()==2,"同种猫应允许多个独立实例");inventory.update("CAT0002","lime","修改后",{"special_one_shot","gunnery_main_1","command_battleship_1"});CommanderCatInventory reloaded(path);reloaded.load();expect(reloaded.cats().size()==2&&reloaded.find("CAT0002")->catId=="lime","库存修改应持久化");reloaded.remove("CAT0001");CommanderCatInventory afterRemove(path);afterRemove.load();expect(afterRemove.cats().size()==1&&!afterRemove.find("CAT0001"),"删除应持久化且不影响其他实例");
    expect(afterRemove.find("CAT0002")->potentialTier.empty()&&!afterRemove.find("CAT0002")->potentialScore,"库存不应保存派生评分");
    {std::ofstream legacy(path,std::ios::trunc);legacy<<"instance_id,cat_id,nickname,initial_talent_ids,potential_score,potential_tier\nCAT0099,oscar,旧库存,special_one_shot + gunnery_main_2,91.2,S\n";}CommanderCatInventory migrated(path);migrated.load();expect(migrated.cats().size()==1&&!migrated.find("CAT0099")->potentialScore,"旧六列库存应忽略过期派生评分");CommanderCatInventory migratedAgain(path);migratedAgain.load();expect(migratedAgain.find("CAT0099")->potentialTier.empty()&&!migratedAgain.find("CAT0099")->potentialScore,"旧库存应在加载时原子迁移为四列事实格式");
    {std::ofstream broken(path,std::ios::trunc);broken<<"bad,header\n";}try{CommanderCatInventory corrupted(path);corrupted.load();throw std::runtime_error("测试失败：损坏库存未报错");}catch(const CommanderCatInventoryError&){ }
}

void testPairing(const CommanderCatDatabase& db){
    CatAssessor assessor(db);CatPairRecommender recommender(db,assessor);std::vector<InventoryCat> cats;
    for(const auto& id:std::vector<std::string>{"pound","antenna","lime","oscar"})cats.push_back({"CAT"+std::to_string(cats.size()+10),id,"",cheapestTargets(db,id,"战列"),std::nullopt,{}});
    const auto result=recommender.recommend("战列",cats);expect(!result.stableChoices.empty(),"库存应生成稳定战列组合");for(const auto& pair:result.stableChoices){expect(pair.commander.instanceId!=pair.staff.instanceId,"同一实例不能重复");expect(pair.commander.catId!=pair.staff.catId,"同种指挥猫不能同队");expect(pair.commanderRoute.slot!=CatSlot::Staff&&pair.staffRoute.slot!=CatSlot::Commander,"职位必须满足一司令一参谋");}
    expect(result.stableChoices.size()<=3,"只输出主选和两个替代组合");expect(result.theoreticalBest.has_value(),"应显示全图鉴理论最佳组合");
    for(std::size_t limit=1;limit<=3;++limit){const auto limited=recommender.recommend("战列",cats,limit);expect(limited.stableChoices.size()==std::min(limit,result.stableChoices.size()),"推荐数量应服从请求上限");for(std::size_t i=0;i<limited.stableChoices.size();++i)expect(limited.stableChoices[i].totalKPercent==result.stableChoices[i].totalKPercent,"限制数量不得改变推荐排序");}
    const auto fallback=recommender.recommend("潜艇",cats,1);expect(!fallback.stableChoices.empty()&&fallback.stableChoices.front().fallbackLevel>0,"缺少标准潜艇猫时应返回库存替补组合");expect(fallback.stableChoices.front().pairFit>0.0,"替补组合应包含适配度");
}

void testSettings(){
    TemporaryDirectory temp;
    const auto path=temp.path()/"commander_cat_settings.csv";
    CommanderCatSettings settings(path);settings.load();
    for(const auto& category:CommanderCatSettings::categories())expect(settings.fleetCount(category)==1,"设置默认应为七类各一队");
    std::map<std::string,int> counts;for(const auto& category:CommanderCatSettings::categories())counts[category]=0;counts["战列"]=2;counts["航母"]=1;counts["突击者"]=1;settings.setFleetCounts(counts);
    CommanderCatSettings reloaded(path);reloaded.load();expect(reloaded.fleetCount("战列")==2&&reloaded.fleetCount("航母")==1&&reloaded.fleetCount("潜艇")==0&&reloaded.fleetCount("突击者")==1,"舰队数量应持久化并在重启后加载");
    try{auto invalid=counts;invalid["战列"]=4;reloaded.setFleetCounts(invalid);throw std::runtime_error("测试失败：非法舰队数量未报错");}catch(const CommanderCatSettingsError&){}
    {std::ofstream broken(path,std::ios::trunc);broken<<"bad,header\n不要覆盖我\n";}
    CommanderCatSettings corrupted(path);corrupted.load();expect(!corrupted.warning().empty()&&corrupted.fleetCount("战列")==1,"损坏设置应使用默认值并显示警告");std::ifstream preserved(path);const std::string text{std::istreambuf_iterator<char>(preserved),{}};expect(text.find("不要覆盖我")!=std::string::npos,"加载损坏设置不得覆盖原文件");
}

void testFleetPlanner(const CommanderCatDatabase& db){
    CatAssessor assessor(db);CatPairRecommender recommender(db,assessor);CatFleetPlanner planner(recommender);
    std::vector<InventoryCat> cats;int serial=1;
    for(const auto& species:db.species()){
        const auto builds=db.buildsFor(species.catId);if(builds.empty())continue;
        std::vector<std::string> talents;for(const auto& group:builds.front()->targetTalentGroups)if(!group.empty())talents.push_back(group.front());
        cats.push_back({"PLAN"+std::to_string(serial++),species.catId,"自动名",talents,std::nullopt,{}});
    }
    std::map<std::string,int> counts;for(const auto& category:CommanderCatSettings::categories())counts[category]=0;counts["战列"]=2;counts["航母"]=1;counts["突击者"]=1;
    const auto result=planner.plan(cats,counts);expect(result.categories.size()==3,"库存规划只应输出数量大于零的类别");std::size_t requested=0,completed=0;std::set<std::string> used;for(const auto& category:result.categories){requested+=category.requested;completed+=category.choices.size();for(const auto& pair:category.choices){expect(used.insert(pair.commander.instanceId).second,"司令实例不得跨队重复");expect(used.insert(pair.staff.instanceId).second,"参谋实例不得跨队重复");expect(pair.commander.catId!=pair.staff.catId,"同队不得使用相同猫种");}}expect(requested==4&&completed==4,"战列2、航母1、突击者1应生成共四支队伍");expect(used.size()==result.usedInstanceIds.size(),"规划结果使用实例清单应与队伍一致");
    const auto theoryBefore=CatGraduationRecommender(db).recommend("潜艇");counts["潜艇"]=0;const auto ignored=planner.plan(cats,counts);static_cast<void>(ignored);const auto theoryAfter=CatGraduationRecommender(db).recommend("潜艇");expect(theoryBefore.generalBest.has_value()==theoryAfter.generalBest.has_value(),"库存舰队设置不得影响理论顶配");if(theoryBefore.generalBest&&theoryAfter.generalBest)expect(theoryBefore.generalBest->totalKPercent==theoryAfter.generalBest->totalKPercent,"理论顶配数值不得随设置改变");
}

}  // namespace

int main(){try{const auto db=CommanderCatDatabase::loadFromDirectory(std::filesystem::path(AZUR_LANE_COMMANDER_CAT_DATA_DIR));testDatabase(db);testAssessment(db);testInventory();testPairing(db);testSettings();testFleetPlanner(db);std::cout<<"All commander cat tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
