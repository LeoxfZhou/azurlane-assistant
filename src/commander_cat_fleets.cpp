#include "commander_cat_fleets.hpp"
#include "commander_cat_embryo.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace {
bool is(const std::string& s, std::initializer_list<const char*> values) {
    return std::any_of(values.begin(), values.end(), [&](auto v){ return s==v; });
}
bool affected(const std::string& family, const FleetShip& ship, const FleetTeam& team) {
    const auto& t=ship.type;
    const bool main=team.kind=="surface"&&ship.slot<3;
    const bool front=team.kind=="surface"&&ship.slot>=3;
    const bool sub=is(t,{"潜艇","潜母"});
    const bool battleship=is(t,{"战列","战巡","航战"});
    const bool cruiser=is(t,{"轻巡","重巡","超巡"});
    const bool destroyer=is(t,{"驱逐","导驱"});
    if(family=="special_new_star"||family=="special_best_friend") return false;
    if(family=="special_wolf_pack") return sub;
    if(family=="special_ace_pilot") return is(t,{"航母","正航"});
    if(family=="special_one_shot") return is(t,{"战列","战巡"});
    if(family=="special_torpedo_soul") return is(t,{"驱逐","导驱","轻巡"});
    if(family=="special_wind") return team.kind=="surface";
    if(family=="special_fire"||family=="special_fate"||family=="special_miracle") return true;
    if(family=="special_fight_on_sight") return cruiser;
    if(family=="special_mountain"||family=="special_forest") return true;
    if(family=="command_faction_eagle") return ship.faction=="白鹰";
    if(family=="command_faction_royal") return ship.faction=="皇家";
    if(family=="command_faction_sakura") return ship.faction=="重樱";
    if(family=="command_faction_ironblood") return ship.faction=="铁血";
    if(family=="maintenance") return is(t,{"航母","正航","轻航"});
    if(family=="gunnery_main") return main&&is(t,{"战列","战巡","航战","重炮","风帆M"});
    if(family=="helm_small"||family=="engine_small") return destroyer||sub||is(t,{"风帆M","风帆V","风帆S"});
    if(family=="helm_medium") return is(t,{"轻巡","重巡","轻航","重炮","维修","运输"});
    if(family=="helm_large") return battleship||is(t,{"航母","正航","超巡"});
    if(family=="cannon_m") return battleship||t=="重炮"||is(t,{"风帆M","风帆V","风帆S"});
    if(family=="cannon_v") return cruiser||is(t,{"风帆M","风帆V","风帆S"});
    if(family=="blue_sea"||family=="tempesta_observer") return true;
    if(family=="blue_sky") return is(t,{"航母","正航","轻航","风帆M"});
    if(family=="fixed_battle_readiness")return is(t,{"战列","战巡","风帆M"});
    if(family=="fixed_sail_banner")return cruiser||t=="风帆V";
    if(family=="fixed_battle_flag")return is(t,{"轻巡","重巡","风帆V"});
    if(family=="fixed_plunder_preparation")return is(t,{"战列","战巡","风帆M","航母","正航"});
    if(family=="fixed_bold_attempt")return is(t,{"航母","正航"})||is(t,{"风帆M","风帆V","风帆S"});
    if(family=="command_battleship")return is(t,{"战列","战巡"});
    if(family=="torpedo_cruiser")return is(t,{"轻巡","重巡"});
    if(family=="aviation_carrier")return is(t,{"航母","正航","轻航"});
    if(family=="aviation_special")return t=="航战";
    if(family=="engine_carrier")return is(t,{"航母","正航","轻航"});
    if(family=="reload_special"||family=="engine_special")return is(t,{"维修","重炮","运输"})||is(t,{"风帆M","风帆V","风帆S"});
    if(family.find("_battleship")!=std::string::npos) return battleship;
    if(family.find("_cruiser")!=std::string::npos) return cruiser;
    if(family.find("_destroyer")!=std::string::npos) return destroyer;
    if(family.find("_submarine")!=std::string::npos) return sub;
    if(family.find("_carrier")!=std::string::npos) return is(t,{"航母","正航"});
    if(family.find("_vanguard")!=std::string::npos) return front;
    if(family.find("_main")!=std::string::npos) return main;
    if(family.find("_special")!=std::string::npos) return is(t,{"维修","重炮"});
    return false;
}
double unit(const std::string& f,const std::string& goal) {
    if(f=="special_fire") return 4;
    if(is(f,{"special_one_shot","special_ace_pilot","special_wolf_pack"})) return 3;
    if(f=="special_fate") return 2;
    if(f=="special_torpedo_soul") return goal=="torpedo"?3:1.5;
    if(f=="special_wind") return goal=="speed"?4:0.5;
    if(is(f,{"special_forest","special_mountain","special_miracle"})) return goal=="survival"?3:1;
    if(f=="special_fight_on_sight") return 1;
    if(f.find("sonar_")==0) return goal=="antisub"?1.5:0.1;
    if(f.find("helm_")==0||f.find("engine_")==0||f.find("aa_")==0)
        return goal=="survival"?1.5:0.4;
    if(f.find("observation_")==0) return 0.75;
    if(f.find("torpedo_")==0) return goal=="torpedo"?1.5:1;
    return 1;
}
FleetCatChoice candidate(const CommanderCatDatabase& db,const CatAssessor& assessor,
    const InventoryCat& cat,const FleetTeam& team,CatSlot slot,bool theoretical,
    const std::map<std::string,std::vector<std::string>>& fixedTargets) {
    FleetCatChoice result;result.cat=cat;
    const auto& species=db.requireSpecies(cat.catId);
    std::vector<std::string> target;
    if(!theoretical&&fixedTargets.count(cat.instanceId)) {
        target=fixedTargets.at(cat.instanceId);
        result.reasons.push_back("使用跨方案统一培养方向，按本队舰娘重新计算收益");
    } else if(species.fixedRoute) {
        const auto nodes=db.fixedRouteFor(cat.catId);
        if(!nodes.empty()) target=nodes.back()->talentIds;
    } else {
        std::map<std::string,int> initial;
        for(const auto& id:cat.initialTalentIds){const auto& t=db.requireTalent(id);initial[t.familyId]=std::max(initial[t.familyId],t.level);}
        std::map<std::string,std::vector<const TalentDefinition*>> families;
        const std::map<std::string,std::string> factionFamilies={{"command_faction_eagle","白鹰"},
            {"command_faction_royal","皇家"},{"command_faction_sakura","重樱"},{"command_faction_ironblood","铁血"}};
        for(const auto& t:db.talents()) {
            if(is(t.familyId,{"cannon_m","cannon_v","blue_sea","blue_sky","tempesta_observer","engine_small","engine_vanguard"}))continue;
            const auto faction=factionFamilies.find(t.familyId);
            if(faction!=factionFamilies.end()&&faction->second!=species.faction&&!initial.count(t.familyId))continue;
            if((t.kind==TalentKind::Normal||t.kind==TalentKind::Special)&&fleetTalentValue(t,team).value>0)
                families[t.familyId].push_back(&t);
        }
        struct Cell {double value=-1;std::vector<std::string> ids;};
        // Even a theoretical cat is limited by its initial points plus six
        // later points; an impossible five level-3 normal target is not ideal.
        const int budget=theoretical?(species.rarity=="SSR"?10:species.rarity=="SR"?8:7):6;
        std::array<std::array<Cell,16>,6> dp;
        dp[0][0].value=0;
        for(const auto& family:families) {
            auto next=dp;
            for(int n=0;n<5;++n) for(int p=0;p<=budget;++p) if(dp[n][p].value>=0)
                for(const auto* t:family.second) {
                    const int cost=std::max(0,t->level-(initial.count(t->familyId)?initial.at(t->familyId):0));
                    if(p+cost>budget)continue;
                    const double value=dp[n][p].value+fleetTalentValue(*t,team).value;
                    auto& cell=next[n+1][p+cost];
                    if(value>cell.value+1e-9){cell=dp[n][p];cell.value=value;cell.ids.push_back(t->talentId);}
                }
            dp=std::move(next);
        }
        Cell best;
        for(int n=1;n<=5;++n)for(int p=0;p<=budget;++p)if(dp[n][p].value>best.value+1e-9)best=dp[n][p];
        target=best.ids;
    }
    RankingRecord synthetic;synthetic.category="实际编队";synthetic.slot=slot;
    synthetic.buildStyle=team.goal;synthetic.stable=true;
    for(const auto& id:target)synthetic.targetTalentGroups.push_back({id});
    result.route=assessor.assessRankingRoute(cat,synthetic);
    if(theoretical){result.route.targetTalentIds=target;result.route.reachable=true;}
    result.route.hasRanking=false;result.route.k1=result.route.k2=result.route.kPercent=0;
    result.route.strengthTier="U";
    for(const auto& id:target) {
        const auto& talent=db.requireTalent(id);
        const auto value=fleetTalentValue(talent,team);
        result.talentValue+=value.value;
        std::string reason=talent.name+" → ";
        for(std::size_t i=0;i<value.beneficiaries.size();++i){if(i)reason+="、";reason+=value.beneficiaries[i];}
        result.reasons.push_back(reason+(value.beneficiaries.empty()?"本队未计收益":""));
    }
    result.skillValue=fleetSkillValue(cat.catId,team,slot,result.reasons).value;
    result.reasons.push_back("本地攻略Lv1摘要："+species.skillLv1Bonus);
    result.reasons.push_back("本地攻略Lv2摘要："+species.skillLv2Bonus);
    result.reasons.push_back("本地攻略Lv3摘要："+species.skillLv3Bonus);
    if(species.fixedRoute) result.reasons.push_back("固定成长按30级天赋；未量化的条件技能不伪造收益");
    return result;
}
}

FleetTalentValue fleetTalentValue(const TalentDefinition& talent,const FleetTeam& team) {
    FleetTalentValue result;
    for(const auto& ship:team.ships)if(affected(talent.familyId,ship,team))result.beneficiaries.push_back(ship.name);
    if(!team.ships.empty())result.value=unit(talent.familyId,team.goal)*talent.level*
        static_cast<double>(result.beneficiaries.size())/team.ships.size();
    return result;
}

FleetTalentValue fleetSkillValue(const std::string& id,const FleetTeam& team,
    CatSlot slot,std::vector<std::string>& reasons) {
    // Explicit skill conditions, independent of guide ranking/recommended role.
    // Checked against https://wiki.biligame.com/blhx/指挥喵 (2026-10-08).
    // Conditional
    // enemy, distance, HP and barrage effects are not assumed active.
    FleetTalentValue result;
    std::set<std::size_t> covered;
    std::size_t effectCoverage=0;
    auto add=[&](CatSlot required,const std::string& label,auto eligible,bool condition=true) {
        if(required!=CatSlot::Either&&required!=slot) {
            reasons.push_back("技能未计入："+label+"（需要"+toText(required)+"位）");return;
        }
        if(!condition){reasons.push_back("技能未计入："+label+"（编队或模式条件不满足）");return;}
        std::string names;
        for(std::size_t i=0;i<team.ships.size();++i)if(eligible(team.ships[i])) {
            ++effectCoverage;covered.insert(i);if(!names.empty())names+="、";names+=team.ships[i].name;
        }
        reasons.push_back(names.empty()?"技能未计入："+label+"（没有受益舰娘）":"技能条件满足："+label+" → "+names);
    };
    const auto carrier=[](const FleetShip& s){return is(s.type,{"航母","正航","轻航"});};
    const auto battleship=[](const FleetShip& s){return is(s.type,{"战列","战巡","航战"});};
    const auto cruiser=[](const FleetShip& s){return is(s.type,{"轻巡","重巡","超巡"});};
    const auto destroyer=[](const FleetShip& s){return is(s.type,{"驱逐","导驱"});};
    const auto submarine=[](const FleetShip& s){return is(s.type,{"潜艇","潜母"});};
    const auto front=[&](const FleetShip& s){return team.kind=="surface"&&s.slot>=3;};
    const bool opsi=team.mode=="opsi";
    if(id=="bishamaru") {
        add(CatSlot::Staff,"Lv1 航母航空、装填",carrier);
        add(CatSlot::Staff,"Lv2 大型作战航母机动",carrier,opsi);
        int mains=0;for(const auto& s:team.ships)if(s.slot<3)++mains;
        add(CatSlot::Either,"Lv3 单航母首轮空袭",carrier,team.kind=="surface"&&mains==1);
    } else if(id=="eagle") {
        add(CatSlot::Staff,"Lv1 空母装填",carrier);
        add(CatSlot::Staff,"Lv2 大型作战空母机动、防空",carrier,opsi);
        add(CatSlot::Staff,"Lv3 空母航空",carrier);
    } else if(id=="lady") {
        add(CatSlot::Staff,"Lv1 空母装填",carrier);add(CatSlot::Staff,"Lv2 空母航空",carrier);add(CatSlot::Staff,"Lv3 空母命中",carrier);
    } else if(id=="jiromaru"||id=="ark") {
        add(CatSlot::Staff,"Lv1 空母命中",carrier);add(CatSlot::Staff,"空母航空",carrier);
    } else if(id=="oscar")add(CatSlot::Staff,"Lv1 战列炮击、命中",battleship);
    else if(id=="lime") {
        add(CatSlot::Staff,"Lv1 战列炮击、防空",battleship);
        add(CatSlot::Staff,"Lv2 大型作战开场30秒炮击",[](const FleetShip& s){return is(s.type,{"战列","战巡","航战","重炮","驱逐","导驱","轻巡","重巡","超巡","风帆M","风帆V","风帆S"});},opsi);
    } else if(id=="marble") {
        add(CatSlot::Staff,"Lv1 大型作战战列机动",battleship,opsi);add(CatSlot::Staff,"Lv2 战列装填",battleship);
        add(CatSlot::Staff,"Lv3 皇家炮击",[](const FleetShip& s){return s.faction=="皇家"&&is(s.type,{"战列","战巡","航战","重炮","驱逐","导驱","轻巡","重巡","超巡"});});
    } else if(id=="potato") {
        add(CatSlot::Staff,"Lv1 战列机动",battleship);add(CatSlot::Staff,"Lv2 战列命中",battleship);
    } else if(id=="katsumaru"||id=="rose") {
        add(CatSlot::Staff,"Lv1 战列后勤",battleship);add(CatSlot::Staff,"Lv2 战列后勤",battleship);add(CatSlot::Staff,"Lv3 战列炮击",battleship);
    } else if(id=="pound") {
        add(CatSlot::Commander,"Lv1 战列炮击、防空",battleship);
        add(CatSlot::Commander,"Lv2 四皇家开场先锋减伤",front,std::count_if(team.ships.begin(),team.ships.end(),[](const FleetShip& s){return s.faction=="皇家";})>=4);
        add(CatSlot::Commander,"Lv3 皇家机动",[](const FleetShip& s){return s.faction=="皇家";});
    } else if(id=="antenna") {
        add(CatSlot::Either,"Lv2 战列旗舰增伤",[&](const FleetShip& s){return s.slot==0&&battleship(s);},team.kind=="surface");
        add(CatSlot::Either,"Lv3 战列机动、防空",battleship);
    } else if(id=="takemaru")add(CatSlot::Commander,"Lv1 巡洋炮击、雷击",cruiser);
    else if(id=="sg") {
        add(CatSlot::Staff,"Lv1 大型作战巡洋机动",cruiser,opsi);add(CatSlot::Staff,"Lv2 巡洋命中",cruiser);add(CatSlot::Staff,"Lv3 巡洋炮击",cruiser);
    } else if(id=="pepper") {
        add(CatSlot::Staff,"Lv1 巡洋机动",cruiser);add(CatSlot::Staff,"Lv3 轻巡重巡雷击",[](const FleetShip& s){return is(s.type,{"轻巡","重巡"});});
    } else if(id=="tofu"||id=="bugles") {
        add(CatSlot::Staff,"Lv1 巡洋属性",cruiser);add(CatSlot::Staff,"Lv2 巡洋属性",cruiser);add(CatSlot::Staff,"Lv3 巡洋属性",cruiser);
    } else if(id=="asamaru") {
        add(CatSlot::Staff,"Lv1 巡洋炮击",cruiser);
        add(CatSlot::Staff,"Lv2 轻巡重巡雷击",[](const FleetShip& s){return is(s.type,{"轻巡","重巡"});});
        add(CatSlot::Staff,"Lv3 轻巡重巡雷击",[](const FleetShip& s){return is(s.type,{"轻巡","重巡"});});
    } else if(id=="yoshimaru") {
        add(CatSlot::Staff,"Lv1 驱逐机动",destroyer);add(CatSlot::Staff,"Lv2 驱逐雷击",destroyer);
        add(CatSlot::Staff,"Lv3 单前排驱逐雷击",[&](const FleetShip& s){return front(s)&&destroyer(s);},std::count_if(team.ships.begin(),team.ships.end(),front)==1);
    } else if(id=="soup")add(CatSlot::Staff,"Lv2 驱逐机动",destroyer);
    else if(id=="beer") {
        add(CatSlot::Staff,"Lv1 驱逐机动",destroyer);add(CatSlot::Staff,"Lv2 驱逐命中",destroyer);add(CatSlot::Staff,"Lv3 驱逐雷击",destroyer);
    } else if(id=="steel") {
        add(CatSlot::Either,"Lv1 潜艇雷击",submarine);
        add(CatSlot::Commander,"Lv2 大型作战潜艇氧气",[](const FleetShip& s){return s.type=="潜艇";},opsi);
    } else if(id=="edelweiss"||id=="gral") {
        add(CatSlot::Staff,"Lv1 潜艇雷击",submarine);add(CatSlot::Staff,id=="gral"?"Lv2 潜艇机动":"Lv2 潜艇命中",submarine);
        if(id=="edelweiss")add(CatSlot::Staff,"Lv3 大型作战潜艇氧气",[](const FleetShip& s){return s.type=="潜艇";},opsi);
    } else if(id=="justice")add(CatSlot::Either,"大型作战三驱逐先锋雷击",front,
        opsi&&std::count_if(team.ships.begin(),team.ships.end(),destroyer)==3);
    else if(id=="bunny")add(CatSlot::Commander,"大型作战驱逐雷击",destroyer,opsi);
    else if(id=="drake") {
        add(CatSlot::Either,"Lv1 战列及风帆M炮击",[&](const FleetShip& s){return battleship(s)||s.type=="风帆M";});
        add(CatSlot::Either,"Lv2 有风帆旗舰减伤",[](const FleetShip& s){return s.slot==0;},std::any_of(team.ships.begin(),team.ships.end(),[](const FleetShip& s){return is(s.type,{"风帆M","风帆V","风帆S"});}));
        add(CatSlot::Either,"Lv3 风帆机动",[](const FleetShip& s){return is(s.type,{"风帆M","风帆V"});});
    } else if(id=="every") {
        add(CatSlot::Either,"Lv1 有风帆先锋减伤",front,std::any_of(team.ships.begin(),team.ships.end(),[](const FleetShip& s){return is(s.type,{"风帆M","风帆V","风帆S"});}));
        add(CatSlot::Commander,"Lv2 舰队炮击",[](const FleetShip& s){return is(s.type,{"战列","战巡","航战","重炮","驱逐","导驱","轻巡","重巡","超巡","风帆M","风帆V","风帆S"});});
    } else if(id=="kidd") {
        add(CatSlot::Either,"Lv1 巡洋及风帆V命中",[&](const FleetShip& s){return cruiser(s)||s.type=="风帆V";});
        add(CatSlot::Either,"Lv3 有风帆巡洋减伤",[](const FleetShip& s){return is(s.type,{"轻巡","重巡","风帆V"});},std::any_of(team.ships.begin(),team.ships.end(),[](const FleetShip& s){return is(s.type,{"风帆M","风帆V","风帆S"});}));
    } else if(id=="teach")add(CatSlot::Either,"Lv2 战列及风帆M炮击",[&](const FleetShip& s){return battleship(s)||s.type=="风帆M";});
    else if(id=="bonny")add(CatSlot::Staff,"Lv2 先锋炮击",front);
    else if(id=="bella"||id=="robert") {
        add(CatSlot::Either,"大型作战潜艇氧气",[](const FleetShip& s){return s.type=="潜艇";},opsi&&team.kind=="submarine");
        if(id=="bella") {
            add(CatSlot::Either,"Lv2 风帆回复",[](const FleetShip& s){return is(s.type,{"风帆M","风帆V","风帆S"});},team.kind=="submarine");
            add(CatSlot::Either,"Lv3 潜艇命中",[&](const FleetShip& s){return submarine(s)||s.type=="风帆S";},team.kind=="submarine");
        } else add(CatSlot::Either,"Lv2 潜艇雷击、风帆S炮击",[&](const FleetShip& s){return submarine(s)||s.type=="风帆S";},team.kind=="submarine");
    }
    for(auto i:covered)result.beneficiaries.push_back(team.ships[i].name);
    if(!team.ships.empty())result.value=2.0*effectCoverage/team.ships.size();
    reasons.push_back("技能启发式分按技能满级估计，不同技能效果分别累计，同一效果内受益舰娘去重；不是伤害百分比或精确强度。敌方类型、距离、血量及弹幕等未建模条件不额外加分。");
    if(covered.empty())reasons.push_back("本职位无已确认的技能覆盖加分；仍可凭天赋参与配队");
    return result;
}

std::vector<FleetTeamResult> recommendRealFleets(const CommanderCatDatabase& db,
    const CatAssessor& assessor,const std::vector<InventoryCat>& inventory,const std::vector<FleetTeam>& teams,bool theoretical,
    const std::map<std::string,std::vector<std::string>>& fixedTargets) {
    if(!theoretical&&std::any_of(teams.begin(),teams.end(),[](const auto& t){return !t.planId.empty();})) {
        std::map<int,std::map<std::string,std::vector<std::size_t>>> groups;
        for(std::size_t i=0;i<teams.size();++i)groups[teams[i].priority][teams[i].planId].push_back(i);
        auto committed=fixedTargets;
        std::map<std::string,std::set<std::string>> used;
        std::vector<FleetTeamResult> output(teams.size());
        for(const auto& [priority,plans]:groups){
            std::map<std::string,std::vector<std::string>> proposed;
            std::map<std::string,std::string> owners;
            for(const auto& [plan,indices]:plans){
                std::vector<InventoryCat> pool;
                for(const auto& cat:inventory)if(!used[plan].count(cat.instanceId))pool.push_back(cat);
                std::vector<FleetTeam> batch;
                for(auto i:indices){auto team=teams[i];team.planId.clear();batch.push_back(team);}
                auto results=recommendRealFleets(db,assessor,pool,batch,false,committed);
                for(std::size_t j=0;j<indices.size();++j){
                    auto result=results[j];result.team=teams[indices[j]];
                    if(result.complete)for(const auto* member:{&result.commander,&result.staff}){
                        const auto& id=member->cat.instanceId;
                        auto target=member->route.targetTalentIds;std::sort(target.begin(),target.end());
                        if(proposed.count(id)&&proposed.at(id)!=target)
                            throw std::runtime_error("同优先级培养路线冲突："+id+"，"+owners[id]+" 与 "+result.team.name+"；请调整分队优先级（数字越小越优先）后重新推荐");
                        proposed[id]=target;owners[id]=result.team.name;used[plan].insert(id);
                    }
                    output[indices[j]]=std::move(result);
                }
            }
            for(const auto& [id,target]:proposed)committed[id]=target;
        }
        return output;
    }
    struct Pair {FleetCatChoice commander,staff;double value;};
    std::vector<std::vector<Pair>> choices(teams.size());
    std::vector<FleetTeamResult> results;
    for(std::size_t i=0;i<teams.size();++i){
        FleetTeamResult result;result.team=teams[i];
        bool front=false,main=false;
        for(const auto& ship:teams[i].ships){front|=ship.slot>=3;main|=ship.slot<3;}
        if(teams[i].ships.empty()||(teams[i].kind=="surface"&&(!front||!main))){
            result.warning="队伍未完成：水面队至少一名前排和一名后排，潜艇队至少一名舰娘。";
        }else{
            std::vector<FleetCatChoice> commanders,staff;
            for(const auto& cat:inventory){
                if(!theoretical&&assessEmbryo(db,cat).uses.empty()&&!db.requireSpecies(cat.catId).fixedRoute)continue;
                commanders.push_back(candidate(db,assessor,cat,teams[i],CatSlot::Commander,theoretical,fixedTargets));
                staff.push_back(candidate(db,assessor,cat,teams[i],CatSlot::Staff,theoretical,fixedTargets));
            }
            for(const auto& c:commanders)for(const auto& s:staff)
                if(c.cat.instanceId!=s.cat.instanceId&&c.cat.catId!=s.cat.catId&&c.route.reachable&&s.route.reachable)
                    choices[i].push_back({c,s,c.talentValue+c.skillValue+s.talentValue+s.skillValue});
            std::stable_sort(choices[i].begin(),choices[i].end(),[](const auto& a,const auto& b){
                if(std::abs(a.value-b.value)>1e-9)return a.value>b.value;
                return std::make_pair(a.commander.cat.instanceId,a.staff.cat.instanceId)<std::make_pair(b.commander.cat.instanceId,b.staff.cat.instanceId);
            });
        }
        results.push_back(result);
    }
    // Scarce teams first, deterministic greedy allocation. Do not claim a
    // global mathematical optimum or probability for this scheduling policy.
    std::vector<std::size_t> order;for(std::size_t i=0;i<teams.size();++i)order.push_back(i);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return choices[a].size()<choices[b].size();});
    std::set<std::string> used;
    std::map<std::string,std::string> eligible;
    for(const auto& list:choices)for(const auto& p:list){eligible[p.commander.cat.instanceId]=p.commander.cat.catId;eligible[p.staff.cat.instanceId]=p.staff.cat.catId;}
    const auto capacity=[&](const std::set<std::string>& occupied){
        int n=0,largest=0;std::map<std::string,int> counts;
        for(const auto& item:eligible)if(!occupied.count(item.first)){++n;largest=std::max(largest,++counts[item.second]);}
        return std::min(n/2,n-largest);
    };
    int remaining=0;for(const auto& list:choices)if(!list.empty())++remaining;
    for(auto i:order){
        if(!choices[i].empty())--remaining;
        const int reserve=std::min(remaining,std::max(0,capacity(used)-1));
        for(const auto& pair:choices[i])if(!used.count(pair.commander.cat.instanceId)&&!used.count(pair.staff.cat.instanceId)){
            auto after=used;after.insert(pair.commander.cat.instanceId);after.insert(pair.staff.cat.instanceId);
            if(!theoretical&&capacity(after)<reserve)continue;
            results[i].complete=true;results[i].commander=pair.commander;results[i].staff=pair.staff;
            if(!theoretical){used.insert(pair.commander.cat.instanceId);used.insert(pair.staff.cat.instanceId);}break;
        }
        if(!results[i].complete&&results[i].warning.empty())results[i].warning="剩余库存不足以组成不同猫种的一司令一参谋；未重复占用其他队的猫。";
    }
    // After coverage, permit only small-cost exchanges between full teams.
    // 5% of the two teams' total utility is the explicit weak-priority budget.
    const auto totalScore=[&](){double sum=0;for(const auto& r:results)if(r.complete)sum+=r.commander.talentValue+r.commander.skillValue+r.staff.talentValue+r.staff.skillValue;return sum;};
    const double baseline=totalScore();
    if(!theoretical) for(std::size_t pass=0;pass<teams.size();++pass)
      for(std::size_t a=0;a<teams.size();++a) for(std::size_t b=0;b<teams.size();++b) {
        if(teams[a].priority>=teams[b].priority||!results[a].complete||!results[b].complete)continue;
        const auto score=[](const FleetCatChoice& c){return c.talentValue+c.skillValue;};
        const double oldA=score(results[a].commander)+score(results[a].staff);
        const double oldTotal=oldA+score(results[b].commander)+score(results[b].staff);
        std::set<std::string> pool={results[a].commander.cat.instanceId,results[a].staff.cat.instanceId,
            results[b].commander.cat.instanceId,results[b].staff.cat.instanceId};
        bool exchanged=false;
        for(const auto& x:choices[a]) {
          if(x.value<=oldA+1e-9)break;
          if(!pool.count(x.commander.cat.instanceId)||!pool.count(x.staff.cat.instanceId))continue;
          for(const auto& y:choices[b]) {
            if(!pool.count(y.commander.cat.instanceId)||!pool.count(y.staff.cat.instanceId))continue;
            if(x.commander.cat.instanceId==y.commander.cat.instanceId||x.commander.cat.instanceId==y.staff.cat.instanceId||
               x.staff.cat.instanceId==y.commander.cat.instanceId||x.staff.cat.instanceId==y.staff.cat.instanceId)continue;
            if(x.value+y.value+1e-9<oldTotal*0.95)continue;
            if(totalScore()-oldTotal+x.value+y.value+1e-9<baseline*0.95)continue;
            results[a].commander=x.commander;results[a].staff=x.staff;
            results[b].commander=y.commander;results[b].staff=y.staff;exchanged=true;break;
          }
          if(exchanged)break;
        }
      }
    return results;
}
