#include "commander_cat_assessment.hpp"
#include "commander_cat_embryo.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

int optionCost(const TalentDefinition& target,
               const std::unordered_map<std::string,const TalentDefinition*>& initial,
               bool& satisfied,
               bool& upgrade,
               bool& missingSpecial) {
    satisfied=false;upgrade=false;missingSpecial=false;
    const auto found=initial.find(target.familyId);
    if(found!=initial.end()){
        const auto* current=found->second;
        if(target.kind==TalentKind::Normal){if(current->level>=target.level){satisfied=true;return 0;}upgrade=true;return target.level-current->level;}
        satisfied=true;return 0;
    }
    if(target.kind==TalentKind::Special||target.kind==TalentKind::Fixed){missingSpecial=true;return 1;}
    if(target.kind==TalentKind::Placeholder)return 0;
    return target.level;
}

const TalentDefinition* lowestTalentInFamily(const CommanderCatDatabase& database,
                                             const std::string& familyId) {
    const TalentDefinition* result = nullptr;
    for (const auto& talent : database.talents()) {
        if (talent.familyId != familyId || talent.kind != TalentKind::Normal) continue;
        if (!result || talent.level < result->level) result = &talent;
    }
    return result;
}

TalentUpgradeAction upgradeAction(const CommanderCatDatabase& database,
                                  const TalentDefinition& from,
                                  const TalentDefinition& target) {
    TalentUpgradeAction result;
    std::vector<const TalentDefinition*> path;
    for (const auto& talent : database.talents()) {
        if (talent.familyId != target.familyId || talent.kind != TalentKind::Normal) continue;
        if (talent.level < from.level || talent.level > target.level) continue;
        path.push_back(&talent);
    }
    std::sort(path.begin(), path.end(), [](const auto* left, const auto* right) {
        return left->level < right->level;
    });
    for (const auto* talent : path) result.pathTalentIds.push_back(talent->talentId);
    if (result.pathTalentIds.empty() || result.pathTalentIds.front() != from.talentId)
        result.pathTalentIds.insert(result.pathTalentIds.begin(), from.talentId);
    if (result.pathTalentIds.back() != target.talentId)
        result.pathTalentIds.push_back(target.talentId);
    result.requiredPoints = std::max(0, target.level - from.level);
    return result;
}

bool routeBetter(const RouteAssessment& a,const RouteAssessment& b){
    if(a.reachable!=b.reachable)return a.reachable;
    if(a.stable!=b.stable)return a.stable;
    if(a.hasRanking!=b.hasRanking)return a.hasRanking;
    if(std::fabs(a.kPercent-b.kPercent)>1e-9)return a.kPercent>b.kPercent;
    if(a.requiredPoints!=b.requiredPoints)return a.requiredPoints<b.requiredPoints;
    return a.buildId<b.buildId;
}

bool slotCanCommand(CatSlot slot){return slot==CatSlot::Commander||slot==CatSlot::Either;}
bool slotCanStaff(CatSlot slot){return slot==CatSlot::Staff||slot==CatSlot::Either;}

bool sameTargets(const std::vector<TalentChoiceGroup>& a,
                 const std::vector<TalentChoiceGroup>& b){
    if(a.size()!=b.size())return false;
    auto normalized=[](const auto& groups){std::vector<std::string> values;for(auto group:groups){std::sort(group.begin(),group.end());std::string value;for(const auto& id:group)value+=id+"|";values.push_back(std::move(value));}std::sort(values.begin(),values.end());return values;};
    return normalized(a)==normalized(b);
}

std::string joinConditions(const std::string& a,const std::string& b){if(a.empty())return b;if(b.empty())return a;if(a==b)return a;return a+"；"+b;}

double initialTalentMatch(const CommanderCatDatabase& database,
                          const InventoryCat& cat,
                          const RouteAssessment& route){
    std::unordered_map<std::string,const TalentDefinition*> initial;
    for(const auto& id:cat.initialTalentIds){const auto& talent=database.requireTalent(id);const auto found=initial.find(talent.familyId);if(found==initial.end()||found->second->level<talent.level)initial[talent.familyId]=&talent;}
    double earned=0.0,total=0.0;
    for(const auto& id:route.targetTalentIds){const auto& target=database.requireTalent(id);const double weight=(target.kind==TalentKind::Special||target.kind==TalentKind::Fixed)?2.0:1.0;total+=weight;const auto found=initial.find(target.familyId);if(found==initial.end())continue;if(target.kind==TalentKind::Normal)earned+=weight*std::min(1.0,static_cast<double>(found->second->level)/std::max(1,target.level));else earned+=weight;}
    return total>0.0?100.0*earned/total:0.0;
}

std::string feasibilityTier(double score){if(score>=85.0)return"S";if(score>=70.0)return"A";if(score>=50.0)return"B";return"C";}
std::string ceilingTier(double score){if(score>=95.0)return"S";if(score>=85.0)return"A";if(score>=70.0)return"B";return"C";}
int rarityDebt(const std::string& id){if(id=="special_fire")return 20;if(id=="special_ace_pilot"||id=="special_one_shot"||id=="special_wolf_pack")return 10;if(id=="special_new_star"||id=="special_best_friend")return 0;if(id=="special_torpedo_soul"||id=="special_wind")return 3;return 6;}
double theoreticalBest(const CommanderCatDatabase& database,const std::string& category){double best=0.0;for(const auto& commander:database.rankings()){if(commander.category!=category||!commander.stable||!slotCanCommand(commander.slot))continue;for(const auto& staff:database.rankings()){if(staff.category!=category||!staff.stable||!slotCanStaff(staff.slot)||commander.catId==staff.catId)continue;best=std::max(best,commander.kPercent+staff.kPercent);}}return best;}
double completedPairValue(const CommanderCatDatabase& database,const std::string& catId,const RouteAssessment& route){double best=0.0;for(const auto& partner:database.rankings()){if(partner.category!=route.category||!partner.stable||partner.catId==catId)continue;const bool valid=(slotCanCommand(route.slot)&&slotCanStaff(partner.slot))||(slotCanStaff(route.slot)&&slotCanCommand(partner.slot));if(valid)best=std::max(best,route.kPercent+partner.kPercent);}return best;}

bool relatedCategory(const std::string& a,const std::string& b){
    if(a==b)return true;const std::set<std::string> main={"战列","航母"};const std::set<std::string> vanguard={"重巡","轻巡","驱逐","突击者"};return(main.count(a)&&main.count(b))||(vanguard.count(a)&&vanguard.count(b));
}

bool slotMatches(CatSlot slot,bool commander){return commander?slotCanCommand(slot):slotCanStaff(slot);}

}  // namespace

CatAssessor::CatAssessor(const CommanderCatDatabase& database):database_(database){}

const RankingRecord* CatAssessor::bestRanking(const BuildPlan& plan) const{
    const RankingRecord* best=nullptr;
    for(const auto* rank:database_.rankingsFor(plan.catId,plan.category)){
        if(!rank->stable)continue;
        if(rank->buildStyle!=plan.buildStyle)continue;
        if(!sameTargets(rank->targetTalentGroups,plan.targetTalentGroups))continue;
        if(!best||rank->kPercent>best->kPercent)best=rank;
    }
    // A different target set cannot lend its measured strength to this plan.
    return best;
}

RouteAssessment CatAssessor::assessTarget(const InventoryCat& cat,
        const std::vector<TalentChoiceGroup>& groups,const std::string& buildId,
        const std::string& category,const std::string& position,const std::string& style,
        const std::string& difficultyFloor,const RankingRecord* ranking,
        const std::string& notes,const std::string& evaluation) const{
    RouteAssessment result;result.buildId=buildId;result.category=category;result.position=position;result.buildStyle=style;result.notes=notes;result.evaluation=evaluation;
    std::unordered_map<std::string,const TalentDefinition*> initial;
    for(const auto& id:cat.initialTalentIds){const auto& talent=database_.requireTalent(id);const auto found=initial.find(talent.familyId);if(found==initial.end()||found->second->level<talent.level)initial[talent.familyId]=&talent;}
    std::set<std::string> targetFamilies;
    bool legalLearning=true;
    const auto& species=database_.requireSpecies(cat.catId);
    const std::map<std::string,std::string> factionFamilies={{"command_faction_eagle","白鹰"},{"command_faction_royal","皇家"},{"command_faction_sakura","重樱"},{"command_faction_ironblood","铁血"}};
    const std::set<std::string> fixedOnly={"cannon_m","cannon_v","blue_sea","blue_sky","tempesta_observer","engine_small","engine_vanguard"};
    // Resolve alternatives, then merge growth chains before charging any points.
    std::vector<const TalentDefinition*> targets;
    for(const auto& group:groups){
        int bestCost=1000;const TalentDefinition* chosen=nullptr;
        for(const auto& id:group){const auto& talent=database_.requireTalent(id);
            if(talent.kind==TalentKind::Placeholder)continue;
            bool satisfied=false,upgrade=false,missingSpecial=false;
            const int cost=optionCost(talent,initial,satisfied,upgrade,missingSpecial);
            if(cost<bestCost){bestCost=cost;chosen=&talent;}
        }
        if(!chosen)continue;
        auto existing=std::find_if(targets.begin(),targets.end(),[&](auto t){return t->familyId==chosen->familyId;});
        if(existing==targets.end())targets.push_back(chosen);
        else if((*existing)->level<chosen->level)*existing=chosen;
    }
    for(const auto* chosen:targets){
        bool chosenSatisfied=false,chosenUpgrade=false,chosenMissingSpecial=false;
        const int bestCost=optionCost(*chosen,initial,chosenSatisfied,chosenUpgrade,chosenMissingSpecial);
        const auto faction=factionFamilies.find(chosen->familyId);
        if(!species.fixedRoute&&((chosen->kind==TalentKind::Fixed||fixedOnly.count(chosen->familyId))||
            (faction!=factionFamilies.end()&&faction->second!=species.faction&&!initial.count(chosen->familyId))))legalLearning=false;
        result.targetTalentIds.push_back(chosen->talentId);
        targetFamilies.insert(chosen->familyId);result.requiredPoints+=bestCost;
        const auto current=initial.find(chosen->familyId);
        if(current!=initial.end())result.retainedTalentIds.push_back(current->second->talentId);
        if(chosenSatisfied)result.satisfiedTalentIds.push_back(chosen->talentId);
        else if(chosenUpgrade){
            result.upgradeTalentIds.push_back(chosen->talentId);
            result.upgradeActions.push_back(upgradeAction(database_,*current->second,*chosen));
        }else{
            result.missingTalentIds.push_back(chosen->talentId);
            const TalentDefinition* acquired=chosen;
            if(chosen->kind==TalentKind::Normal){
                if(const auto* first=lowestTalentInFamily(database_,chosen->familyId))acquired=first;
                if(acquired->level<chosen->level){
                    result.upgradeTalentIds.push_back(chosen->talentId);
                    result.upgradeActions.push_back(upgradeAction(database_,*acquired,*chosen));
                }
            }
            result.acquireTalentIds.push_back(acquired->talentId);
        }
        if(chosenMissingSpecial)++result.missingSpecialCount;
    }
    // 优先列表只包含真正需要获取或升级的目标天赋；待覆盖天赋单独输出。
    for(const auto& id:result.acquireTalentIds){const auto& t=database_.requireTalent(id);if(t.kind==TalentKind::Special||t.kind==TalentKind::Fixed)result.priorityTalentIds.push_back(id);}
    for(const auto& id:result.acquireTalentIds){const auto& t=database_.requireTalent(id);if(t.kind==TalentKind::Normal)result.priorityTalentIds.push_back(id);}
    result.priorityTalentIds.insert(result.priorityTalentIds.end(),result.upgradeTalentIds.begin(),result.upgradeTalentIds.end());
    for(const auto& [family,talent]:initial)if(!targetFamilies.count(family))result.unrelatedInitialTalentIds.push_back(talent->talentId);
    std::sort(result.unrelatedInitialTalentIds.begin(),result.unrelatedInitialTalentIds.end());
    for(const auto& id:result.missingTalentIds)if(database_.requireTalent(id).kind==TalentKind::Normal)++result.newNormalFamilyCount;
    const auto slotsAfterLearning=initial.size()+result.acquireTalentIds.size();
    result.overwriteCount=slotsAfterLearning>5?static_cast<int>(slotsAfterLearning-5):0;
    auto replaceCandidates=result.unrelatedInitialTalentIds;
    std::stable_sort(replaceCandidates.begin(),replaceCandidates.end(),[&](const auto& leftId,const auto& rightId){
        const auto& left=database_.requireTalent(leftId);const auto& right=database_.requireTalent(rightId);
        const auto kindRank=[](TalentKind kind){return kind==TalentKind::Normal?0:kind==TalentKind::Special?1:2;};
        if(kindRank(left.kind)!=kindRank(right.kind))return kindRank(left.kind)<kindRank(right.kind);
        if(left.level!=right.level)return left.level<right.level;
        return left.talentId<right.talentId;
    });
    for(std::size_t i=0;i<replaceCandidates.size();++i){
        if(i<static_cast<std::size_t>(result.overwriteCount))result.replacementTalentIds.push_back(replaceCandidates[i]);
        else result.flexibleInitialTalentIds.push_back(replaceCandidates[i]);
    }
    const bool legalFiveSlots=targetFamilies.size()<=5&&initial.size()<=5&&
        result.overwriteCount<=static_cast<int>(result.unrelatedInitialTalentIds.size());
    result.reachable=result.requiredPoints<=6&&legalFiveSlots&&legalLearning;
    if(!result.reachable)result.difficulty=CatDifficulty::Unreachable;
    else if(result.missingSpecialCount>=3)result.difficulty=CatDifficulty::VeryHard;
    else if(result.missingSpecialCount==0&&result.requiredPoints<=4)result.difficulty=CatDifficulty::Easy;
    else if(result.missingSpecialCount<=1&&result.requiredPoints<=5)result.difficulty=CatDifficulty::Medium;
    else result.difficulty=CatDifficulty::Hard;
    if(result.reachable&&difficultyFloor=="难"&&(result.difficulty==CatDifficulty::Easy||result.difficulty==CatDifficulty::Medium))result.difficulty=CatDifficulty::Hard;
    if(ranking){result.slot=ranking->slot;result.stable=ranking->stable;result.hasRanking=true;result.rank=ranking->rank;result.k1=ranking->k1;result.k2=ranking->k2;result.kPercent=ranking->kPercent;result.scenario=ranking->scenario;int size=0;int stablePosition=0;std::vector<const RankingRecord*> values;for(const auto& r:database_.rankings())if(r.category==category&&r.stable)values.push_back(&r);std::sort(values.begin(),values.end(),[](auto a,auto b){return a->kPercent>b->kPercent;});size=static_cast<int>(values.size());for(std::size_t i=0;i<values.size();++i)if(values[i]==ranking){stablePosition=static_cast<int>(i)+1;break;}result.categorySize=size;result.stableRank=stablePosition;const double percentile=size&&stablePosition?static_cast<double>(stablePosition)/size:1.0;result.strengthTier=percentile<=0.20?"S":percentile<=0.50?"A":percentile<=0.80?"B":"C";}
    for(const auto* guide:database_.guideRecommendationsFor(cat.catId)){if(!guide->covered)continue;if(result.guideRecommendation.empty()||guide->recommendation=="不建议洗"){result.guideScenario=guide->scenario;result.guideRecommendation=guide->recommendation;if(!guide->notes.empty())result.guideRecommendation+="："+guide->notes;}}
    return result;
}

RouteAssessment CatAssessor::assessRankingRoute(const InventoryCat& cat,const RankingRecord& ranking) const{
    InventoryCat effective=cat;
    if(database_.requireSpecies(cat.catId).fixedRoute){const auto fixed=database_.fixedRouteFor(cat.catId);if(!fixed.empty())effective.initialTalentIds=fixed.back()->talentIds;}
    auto result=assessTarget(effective,ranking.targetTalentGroups,"ranking_"+ranking.category+"_"+std::to_string(ranking.rank),ranking.category,ranking.position,ranking.buildStyle,"",&ranking,"","");
    if(database_.requireSpecies(cat.catId).fixedRoute){
        result.buildId="fixed_"+cat.catId;result.retainedTalentIds=cat.initialTalentIds;
        result.satisfiedTalentIds.clear();
        for(const auto& id:result.targetTalentIds)if(std::find(cat.initialTalentIds.begin(),cat.initialTalentIds.end(),id)!=cat.initialTalentIds.end())result.satisfiedTalentIds.push_back(id);
    }
    return result;
}

AssessmentResult CatAssessor::assess(const InventoryCat& cat) const{
    const auto& species=database_.requireSpecies(cat.catId);AssessmentResult result;result.instanceId=cat.instanceId;result.catId=cat.catId;result.fixedRoute=species.fixedRoute;
    InventoryCat effective=cat;
    if(species.fixedRoute){const auto fixedRoute=database_.fixedRouteFor(cat.catId);if(!fixedRoute.empty())effective.initialTalentIds=fixedRoute.back()->talentIds;}
    for(const auto* plan:database_.buildsFor(cat.catId))result.routes.push_back(assessTarget(effective,plan->targetTalentGroups,plan->buildId,plan->category,plan->position,plan->buildStyle,plan->difficultyFloor,bestRanking(*plan),plan->notes,plan->evaluation));
    if(species.fixedRoute&&result.routes.empty()){const auto route=database_.fixedRouteFor(cat.catId);if(!route.empty()){RouteAssessment fixed;fixed.buildId="fixed_"+cat.catId;fixed.category="固定";fixed.position="固定成长";fixed.buildStyle="固定";fixed.reachable=true;fixed.difficulty=CatDifficulty::Easy;fixed.strengthTier="U";fixed.targetTalentIds=route.back()->talentIds;fixed.retainedTalentIds=cat.initialTalentIds;result.routes.push_back(std::move(fixed));}}
    if(species.fixedRoute)for(auto& route:result.routes){route.buildId="fixed_"+cat.catId;route.retainedTalentIds=cat.initialTalentIds;route.satisfiedTalentIds.clear();}
    std::vector<std::size_t> order;for(std::size_t i=0;i<result.routes.size();++i)order.push_back(i);std::sort(order.begin(),order.end(),[&](auto a,auto b){return routeBetter(result.routes[a],result.routes[b]);});
    for(const auto i:order)if(result.routes[i].reachable&&result.routes[i].stable){result.mainRoute=i;break;}
    if(!result.mainRoute)for(const auto i:order)if(result.routes[i].reachable){result.mainRoute=i;break;}
    for(const auto i:order){if(result.mainRoute&&i==*result.mainRoute)continue;if(result.routes[i].reachable){result.alternatives.push_back(i);if(result.alternatives.size()==2)break;}}
    return result;
}

PotentialAssessment CatAssessor::assessPotential(const InventoryCat& cat) const{
    auto assessment=assess(cat);PotentialAssessment best;const auto& species=database_.requireSpecies(cat.catId);
    const auto embryo=assessEmbryo(database_,cat);
    best.embryoNicknamePrefix=embryo.nicknamePrefix;
    best.embryoExplanation=embryo.explanation;
    best.protectedRare=embryo.protectedRare;
    best.tier=embryo.tier;
    if(species.fixedRoute){best.fixedGrowth=true;best.hasRoute=!assessment.routes.empty();best.tier="固定";if(best.hasRoute){best.route=assessment.routes.front();best.finalRoute=best.route;best.practicalRoute=best.route;}return best;}
    std::unordered_map<std::string,const TalentDefinition*> initial;for(const auto& id:cat.initialTalentIds){const auto& t=database_.requireTalent(id);const auto found=initial.find(t.familyId);if(found==initial.end()||found->second->level<t.level)initial[t.familyId]=&t;}
    for(auto& route:assessment.routes){int debt=0;for(const auto& id:route.missingTalentIds){const auto& t=database_.requireTalent(id);debt+=t.kind==TalentKind::Special?rarityDebt(id):3;}route.feasibilityScore=route.reachable?std::max(0.0,100.0-8.0*route.requiredPoints-debt):0.0;route.feasibilityTier=feasibilityTier(route.feasibilityScore);if(route.hasRanking&&route.stable){const double top=theoreticalBest(database_,route.category);const double pair=completedPairValue(database_,cat.catId,route);if(top>0.0&&pair>0.0){route.ceilingQuantified=true;route.ceilingScore=std::min(100.0,100.0*pair/top);route.ceilingTier=ceilingTier(route.ceilingScore);}}bool high=false;int useful=0;for(const auto& [family,t]:initial){if(t->talentId=="special_fire"||t->talentId=="special_ace_pilot"||t->talentId=="special_one_shot"||t->talentId=="special_wolf_pack")high=true;for(const auto& targetId:route.targetTalentIds){const auto& target=database_.requireTalent(targetId);if(target.kind==TalentKind::Normal&&target.familyId==family)useful+=std::min(t->level,target.level);}}const int threshold=species.rarity=="SSR"?2:((species.rarity=="SR"&&(route.category=="航母"||route.category=="潜艇"))?1:99);route.nearGraduationStart=high&&useful>=threshold;}
    std::vector<const RouteAssessment*> candidates;for(const auto& route:assessment.routes)if(route.hasRanking)candidates.push_back(&route);if(candidates.empty()){if(!assessment.routes.empty()){best.hasRoute=true;best.route=assessment.routes.front();best.finalRoute=best.route;best.practicalRoute=best.route;}return best;}
    const RouteAssessment* final=*std::max_element(candidates.begin(),candidates.end(),[](auto a,auto b){if(a->reachable!=b->reachable)return !a->reachable;if(a->ceilingQuantified!=b->ceilingQuantified)return !a->ceilingQuantified;if(std::fabs(a->ceilingScore-b->ceilingScore)>1e-9)return a->ceilingScore<b->ceilingScore;return a->requiredPoints>b->requiredPoints;});
    std::vector<const RouteAssessment*> strong;for(auto route:candidates)if(route->reachable&&route->ceilingQuantified&&route->ceilingScore>=85.0)strong.push_back(route);const RouteAssessment* practical=nullptr;if(!strong.empty())practical=*std::max_element(strong.begin(),strong.end(),[](auto a,auto b){const bool ar=a->guideRecommendation.rfind("不建议洗",0)!=0,br=b->guideRecommendation.rfind("不建议洗",0)!=0;if(ar!=br)return !ar;if(std::fabs(a->feasibilityScore-b->feasibilityScore)>1e-9)return a->feasibilityScore<b->feasibilityScore;return a->ceilingScore<b->ceilingScore;});else practical=*std::max_element(candidates.begin(),candidates.end(),[](auto a,auto b){const double as=0.6*a->feasibilityScore+0.4*a->ceilingScore,bs=0.6*b->feasibilityScore+0.4*b->ceilingScore;if(std::fabs(as-bs)>1e-9)return as<bs;return a->requiredPoints>b->requiredPoints;});
    best.hasRoute=true;best.quantified=true;best.finalRoute=*final;best.practicalRoute=*practical;best.route=*final;best.score=final->feasibilityScore;best.ceilingQuantified=final->ceilingQuantified;best.ceilingScore=final->ceilingScore;best.ceilingTier=final->ceilingTier;best.nearGraduationStart=final->nearGraduationStart;best.routesDiffer=final->buildId!=practical->buildId;return best;
}

std::string CatAssessor::automaticNickname(const RouteAssessment& route) const{
    std::string category=route.category=="战列"?"炮":route.category=="航母"?"航":route.category=="潜艇"?"潜":route.category=="重巡"?"重雷":route.category=="轻巡"?"轻雷":route.category=="驱逐"?"驱雷":route.category=="突击者"?"突":"定";
    const std::string slot=route.slot==CatSlot::Commander?"司":route.slot==CatSlot::Staff?"参":"通";
    return category+slot+route.buildStyle+route.strengthTier;
}

std::string CatAssessor::nicknameBase(const PotentialAssessment& potential) const{
    if(potential.fixedGrowth)return"定兼辅固";
    if(!potential.embryoNicknamePrefix.empty())return potential.embryoNicknamePrefix+potential.tier;
    if(!potential.hasRoute||!potential.route.reachable)return"定兼辅备";
    const auto& route=potential.route;
    const std::string category=route.category=="战列"?"战":route.category=="航母"?"航":route.category=="潜艇"?"潜":route.category=="重巡"?"重":route.category=="轻巡"?"轻":route.category=="驱逐"?"驱":route.category=="突击者"?"突":"定";
    const std::string slot=route.slot==CatSlot::Commander?"司":route.slot==CatSlot::Staff?"参":"兼";
    const std::string style=route.buildStyle.find("装填")!=std::string::npos?"装":route.buildStyle.find("航速")!=std::string::npos?"速":route.buildStyle.find("纯伤")!=std::string::npos?"伤":"辅";
    std::string result=category+slot+style+potential.tier;
    const auto blocked=result.find("装B");if(blocked!=std::string::npos)result.replace(blocked,std::string("装B").size(),"装乙");
    return result;
}

std::string CatAssessor::uniqueNickname(
        const PotentialAssessment& potential,
        const std::vector<InventoryCat>& inventory,
        const std::string& excludedInstance) const {
    const auto base=nicknameBase(potential);std::set<std::string> used;
    for(const auto& cat:inventory)if(cat.instanceId!=excludedInstance)used.insert(cat.nickname);
    for(std::size_t serial=1;;++serial){const auto candidate=base+std::to_string(serial);if(database_.nicknameAllowed(candidate)&&!used.count(candidate))return candidate;}
}

CatPairRecommender::CatPairRecommender(const CommanderCatDatabase& database,const CatAssessor& assessor):database_(database),assessor_(assessor){}

PairRecommendation CatPairRecommender::recommend(const std::string& category,const std::vector<InventoryCat>& inventory,std::size_t maxStableChoices) const{
    PairRecommendation result;result.category=category;
    struct Candidate{const InventoryCat* cat=nullptr;RouteAssessment route;double fit=0.0;int level=5;std::string label;bool comparableK=false;};
    auto candidateFor=[&](const InventoryCat& cat,bool commander){
        Candidate best;best.cat=&cat;const auto assessment=assessor_.assess(cat);
        for(const auto& route:assessment.routes){
            const bool exact=route.category==category;const bool role=slotMatches(route.slot,commander);int level=4;double base=30.0;std::string label="通用替补";
            if(exact&&role&&route.reachable&&route.stable&&route.hasRanking){level=0;base=100.0;label="完全适配";}
            else if(exact&&role){level=1;base=route.reachable?90.0:82.0;label=route.reachable?"同类接近理想":"同类但毕业较难";}
            else if(exact){level=2;base=70.0;label=commander?"职位替补为司令":"职位替补为参谋";}
            else if(relatedCategory(category,route.category)){level=3;base=50.0;label="相邻舰种替补";}
            const double match=route.targetTalentIds.empty()?50.0:initialTalentMatch(database_,cat,route);const double fit=0.7*base+0.3*match;
            if(level<best.level||(level==best.level&&(fit>best.fit+1e-9||(std::fabs(fit-best.fit)<1e-9&&route.kPercent>best.route.kPercent)))){best.route=route;best.fit=fit;best.level=level;best.label=label;best.comparableK=exact&&route.hasRanking;}
        }
        return best;
    };
    std::vector<Candidate> commanders,staffs;for(const auto& cat:inventory){auto commander=candidateFor(cat,true);auto staff=candidateFor(cat,false);if(commander.level<5)commanders.push_back(std::move(commander));if(staff.level<5)staffs.push_back(std::move(staff));}
    std::vector<PairChoice> choices;
    for(const auto& commander:commanders)for(const auto& staff:staffs){
        if(commander.cat->instanceId==staff.cat->instanceId||commander.cat->catId==staff.cat->catId)continue;
        PairChoice choice;choice.commander=*commander.cat;choice.staff=*staff.cat;choice.commanderRoute=commander.route;choice.staffRoute=staff.route;choice.commanderFit=commander.fit;choice.staffFit=staff.fit;choice.pairFit=(commander.fit+staff.fit)/2.0;choice.fallbackLevel=std::max(commander.level,staff.level);choice.commanderFitLabel=commander.label;choice.staffFitLabel=staff.label;choice.hasComparableKPercent=commander.comparableK&&staff.comparableK;choice.totalKPercent=choice.hasComparableKPercent?commander.route.kPercent+staff.route.kPercent:0.0;choice.conditions=joinConditions(commander.route.scenario,staff.route.scenario);choices.push_back(std::move(choice));
    }
    std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){if(a.fallbackLevel!=b.fallbackLevel)return a.fallbackLevel<b.fallbackLevel;if(a.fallbackLevel==0&&a.hasComparableKPercent&&b.hasComparableKPercent&&std::fabs(a.totalKPercent-b.totalKPercent)>1e-9)return a.totalKPercent>b.totalKPercent;if(std::fabs(a.pairFit-b.pairFit)>1e-9)return a.pairFit>b.pairFit;if(a.hasComparableKPercent!=b.hasComparableKPercent)return a.hasComparableKPercent;if(std::fabs(a.totalKPercent-b.totalKPercent)>1e-9)return a.totalKPercent>b.totalKPercent;if(a.commander.catId!=b.commander.catId)return a.commander.catId<b.commander.catId;return a.staff.catId<b.staff.catId;});
    std::set<std::string> seen;for(auto& choice:choices){const auto first=std::min(choice.commander.catId,choice.staff.catId);const auto second=std::max(choice.commander.catId,choice.staff.catId);if(!seen.insert(first+"|"+second).second)continue;result.stableChoices.push_back(std::move(choice));if(result.stableChoices.size()>=maxStableChoices)break;}
    std::vector<const RankingRecord*> theoretical;for(const auto& rank:database_.rankings())if(rank.category==category&&rank.stable)theoretical.push_back(&rank);double best=-1;for(const auto* a:theoretical)for(const auto* b:theoretical){if(a->catId==b->catId||!slotCanCommand(a->slot)||!slotCanStaff(b->slot))continue;const double score=a->kPercent+b->kPercent;if(score>best){best=score;TheoreticalPair pair;pair.commanderCatId=a->catId;pair.staffCatId=b->catId;pair.totalKPercent=score;result.theoreticalBest=pair;}}
    if(result.theoreticalBest){std::set<std::string> owned;for(const auto& cat:inventory)owned.insert(cat.catId);if(!owned.count(result.theoreticalBest->commanderCatId))result.theoreticalBest->missingCatIds.push_back(result.theoreticalBest->commanderCatId);if(!owned.count(result.theoreticalBest->staffCatId))result.theoreticalBest->missingCatIds.push_back(result.theoreticalBest->staffCatId);}
    return result;
}

CatFleetPlanner::CatFleetPlanner(const CatPairRecommender& recommender)
    : recommender_(recommender) {}

InventoryFleetPlan CatFleetPlanner::plan(
        const std::vector<InventoryCat>& inventory,
        const std::map<std::string, int>& requestedCounts) const {
    InventoryFleetPlan result;
    static const std::vector<std::string> categoryOrder = {
        "战列", "航母", "潜艇", "重巡", "轻巡", "驱逐", "突击者"};
    for (const auto& category : categoryOrder) {
        const auto found = requestedCounts.find(category);
        const std::size_t requested = found == requestedCounts.end()
            ? 0U : static_cast<std::size_t>(std::max(0, found->second));
        if (requested > 0) result.categories.push_back({category, requested, {}});
    }

    std::set<std::string> used;
    auto remainingInventory = [&]() {
        std::vector<InventoryCat> remaining;
        for (const auto& cat : inventory)
            if (!used.count(cat.instanceId)) remaining.push_back(cat);
        return remaining;
    };

    for (std::size_t round = 0; round < 3; ++round) {
        struct Pending {
            std::size_t resultIndex = 0;
            std::size_t available = 0;
            std::size_t categoryOrder = 0;
        };
        std::vector<Pending> pending;
        for (std::size_t i = 0; i < result.categories.size(); ++i) {
            auto& category = result.categories[i];
            if (category.requested <= round || category.choices.size() > round) continue;
            const auto recommendation = recommender_.recommend(
                category.category, remainingInventory(), inventory.size());
            pending.push_back({i, recommendation.stableChoices.size(), i});
        }
        std::sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) {
            if (a.available != b.available) return a.available < b.available;
            return a.categoryOrder < b.categoryOrder;
        });
        for (const auto& item : pending) {
            auto& category = result.categories[item.resultIndex];
            const auto recommendation = recommender_.recommend(
                category.category, remainingInventory(), 1);
            if (recommendation.stableChoices.empty()) continue;
            const auto& choice = recommendation.stableChoices.front();
            if (used.count(choice.commander.instanceId) || used.count(choice.staff.instanceId))
                continue;
            used.insert(choice.commander.instanceId);
            used.insert(choice.staff.instanceId);
            category.choices.push_back(choice);
        }
    }
    result.usedInstanceIds.assign(used.begin(), used.end());
    return result;
}

CatGraduationRecommender::CatGraduationRecommender(const CommanderCatDatabase& database):database_(database){}

GraduationRecommendation CatGraduationRecommender::recommend(const std::string& category) const{
    GraduationRecommendation result;result.category=category;
    auto bestPair=[&](bool stableOnly)->std::optional<GraduationPair>{
        const RankingRecord* bestCommander=nullptr;const RankingRecord* bestStaff=nullptr;double best=-1.0;
        for(const auto& commander:database_.rankings()){if(commander.category!=category||(stableOnly&&!commander.stable)||!slotCanCommand(commander.slot))continue;for(const auto& staff:database_.rankings()){if(staff.category!=category||(stableOnly&&!staff.stable)||!slotCanStaff(staff.slot)||commander.catId==staff.catId)continue;const double score=commander.kPercent+staff.kPercent;if(score>best){best=score;bestCommander=&commander;bestStaff=&staff;}}}
        if(!bestCommander||!bestStaff)return std::nullopt;GraduationPair pair;pair.commander=*bestCommander;pair.staff=*bestStaff;pair.totalKPercent=best;pair.conditions=joinConditions(bestCommander->scenario,bestStaff->scenario);return pair;
    };
    result.generalBest=bestPair(true);const auto overall=bestPair(false);
    if(overall&&result.generalBest&&overall->totalKPercent>result.generalBest->totalKPercent+1e-9&&(overall->commander.rank!=result.generalBest->commander.rank||overall->staff.rank!=result.generalBest->staff.rank))result.conditionalBest=overall;
    else if(overall&&!result.generalBest)result.conditionalBest=overall;

    auto bestPartner=[&](bool needsCommander)->const RankingRecord*{const RankingRecord* best=nullptr;for(const auto& rank:database_.rankings()){if(rank.category!=category||!rank.stable)continue;if(needsCommander&&!slotCanCommand(rank.slot))continue;if(!needsCommander&&!slotCanStaff(rank.slot))continue;if(!best||rank.kPercent>best->kPercent)best=&rank;}return best;};
    for(const auto* recommendation:database_.fixedRecommendationsFor(category)){
        const RankingRecord* partner=nullptr;CatSlot assigned=recommendation->slot;
        if(recommendation->slot==CatSlot::Commander)partner=bestPartner(false);
        else if(recommendation->slot==CatSlot::Staff)partner=bestPartner(true);
        else{const auto* staffPartner=bestPartner(false);const auto* commanderPartner=bestPartner(true);if(staffPartner&&(!commanderPartner||staffPartner->kPercent>=commanderPartner->kPercent)){assigned=CatSlot::Commander;partner=staffPartner;}else{assigned=CatSlot::Staff;partner=commanderPartner;}}
        if(!partner)continue;const auto route=database_.fixedRouteFor(recommendation->catId);if(route.empty())continue;FixedScenarioPair pair;pair.recommendation=*recommendation;pair.assignedSlot=assigned;pair.partner=*partner;pair.fixedTargetTalentIds=route.back()->talentIds;result.fixedScenarios.push_back(std::move(pair));
    }
    return result;
}
