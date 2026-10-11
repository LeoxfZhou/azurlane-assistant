#include "commander_cat_fleets.hpp"
#include "commander_cat_embryo.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main(){
    const auto db=CommanderCatDatabase::loadFromDirectory(AZUR_LANE_COMMANDER_CAT_DATA_DIR);
    const CatAssessor assessor(db);
    FleetTeam team{"one","混编","surface","damage","opsi",{
        {"bb","战列舰","皇家","战列",0},{"cl","轻巡舰","白鹰","轻巡",3}}};
    const auto soul=fleetTalentValue(db.requireTalent("special_torpedo_soul"),team);
    check(soul.beneficiaries==std::vector<std::string>{"轻巡舰"},"soul targets DD/CL only");
    FleetTeam low{"low","低耗","surface","damage","campaign",{
        {"r","突击者","白鹰","轻航",0},{"dd","驱逐舰","皇家","驱逐",3}}};
    check(fleetTalentValue(db.requireTalent("special_ace_pilot"),low).value==0,"CVL cannot borrow CV special");
    check(fleetTalentValue(db.requireTalent("gunnery_main_3"),low).value==0,"CVL has no useful gun stat");
    check(fleetTalentValue(db.requireTalent("special_fight_on_sight"),low).value==0,"fight special cruiser only");
    check(fleetTalentValue(db.requireTalent("special_new_star"),team).value==0,"XP not combat");
    std::vector<std::string> skillReasons;
    auto skills=[&](const std::string& id,const FleetTeam& fleet,CatSlot slot){
        skillReasons.clear();return fleetSkillValue(id,fleet,slot,skillReasons).value;
    };
    FleetTeam carriers=low;
    carriers.ships.push_back({"cv","正航","重樱","航母",1});
    check(skills("bishamaru",carriers,CatSlot::Commander)==0,"Bishamaru commander has no multi-main carrier skill");
    check(skills("bishamaru",carriers,CatSlot::Staff)>0,"Bishamaru staff activates carrier skill");
    check(skills("bishamaru",low,CatSlot::Commander)>0,"single carrier skill does not require staff");
    auto mixed=low;mixed.ships.push_back({"bb","战列","皇家","战列",2});
    check(skills("bishamaru",mixed,CatSlot::Commander)==0,"one carrier plus battleship is not single-main condition");
    check(skills("bishamaru",team,CatSlot::Staff)==0,"no carriers means no carrier benefit");
    auto opsiCarriers=carriers;opsiCarriers.mode="opsi";
    const auto coverageScore=skills("bishamaru",opsiCarriers,CatSlot::Staff);
    check(std::abs(coverageScore-8.0/3)<1e-6,"distinct skills on same ship must contribute separately");
    check(skills("oscar",team,CatSlot::Commander)==0,"Oscar is not a commander skill cat");
    check(skills("oscar",team,CatSlot::Staff)>0,"Oscar staff benefit");
    check(skills("takemaru",team,CatSlot::Staff)==0,"Takemaru requires commander");
    check(skills("pound",team,CatSlot::Staff)==0,"Pound requires commander");
    check(skills("bunny",low,CatSlot::Commander)==0,"campaign cannot borrow opsi skills");
    check(skills("justice",team,CatSlot::Commander)==0,"Justice cannot borrow guide destroyer ranking without three destroyers");
    check(skills("unknown",team,CatSlot::Commander)==0,"unknown skill must not get guide fallback points");
    check(skills("bishamaru",low,CatSlot::Staff)>skills("bishamaru",low,CatSlot::Commander),"Bishamaru single carrier staff strictly dominates commander");
    FleetTeam subs{"sub","潜艇","submarine","damage","opsi",{{"ss","潜艇","铁血","潜艇",0}}};
    check(skills("steel",subs,CatSlot::Commander)>skills("steel",subs,CatSlot::Staff),"Steel opsi commander oxygen must not disappear into torpedo coverage");
    check(skills("pepper",FleetTeam{"cb","超巡","surface","damage","campaign",{{"cb","超巡","皇家","超巡",3}}},CatSlot::Staff)==2,"supercruiser cannot receive Pepper torpedo skill");
    std::vector<InventoryCat> rangerPair={
        {"CAT0004","bishamaru","",{"special_fire","aviation_carrier_1","maintenance_1"},{},{}},
        {"CAT0035","justice","",{"special_fire","command_destroyer_1","helm_small_1"},{},{}}};
    const auto ranger=recommendRealFleets(db,assessor,rangerPair,{low});
    check(ranger[0].complete&&ranger[0].staff.cat.catId=="bishamaru","Ranger pair assigns Bishamaru staff, independent of instance ordering");
    std::vector<InventoryCat> inventory={
        {"A","oscar","",{"special_fire","command_cruiser_2","gunnery_main_1"},{},{}},
        {"B","takemaru","",{"special_torpedo_soul","special_fate","gunnery_cruiser_1"},{},{}},
        {"C","oscar","",{"special_one_shot","command_battleship_2","gunnery_main_1"},{},{}},
        {"D","bishamaru","",{"special_ace_pilot","special_fire","aviation_carrier_1"},{},{}}};
    const auto before=assessEmbryo(db,inventory.front());
    const auto roles=recommendRealFleets(db,assessor,{inventory[0],inventory[3]},{carriers});
    check(roles[0].complete&&roles[0].staff.cat.catId=="bishamaru","pair enumeration must prefer Bishamaru staff when only that role activates skills");
    const auto plan=recommendRealFleets(db,assessor,inventory,{team,low});
    std::set<std::string> used;
    for(const auto& result:plan){
        check(result.complete,"two complete teams with sufficient distinct species");
        check(result.commander.cat.catId!=result.staff.cat.catId,"different species in pair");
        for(const auto* member:{&result.commander,&result.staff}){
            check(used.insert(member->cat.instanceId).second,"global instance uniqueness");
            check(member->route.requiredPoints<=6&&member->route.reachable,"reachable target");
            check(!member->route.hasRanking&&member->route.kPercent==0,"no fabricated k for generated targets");
            std::set<std::string> families;
            for(const auto& id:member->route.targetTalentIds)check(families.insert(db.requireTalent(id).familyId).second,"unique families");
            for(const auto& id:member->route.acquireTalentIds){
                const auto& f=db.requireTalent(id).familyId;
                const auto& faction=db.requireSpecies(member->cat.catId).faction;
                check(f!="command_faction_eagle"||faction=="白鹰","no cross-faction Eagle learning");
                check(f!="command_faction_royal"||faction=="皇家","no cross-faction Royal learning");
                check(f!="command_faction_sakura"||faction=="重樱","no cross-faction Sakura learning");
                check(f!="command_faction_ironblood"||faction=="铁血","no cross-faction Ironblood learning");
                check(f!="blue_sea"&&f!="blue_sky"&&f!="tempesta_observer"&&f!="cannon_m"&&f!="cannon_v","fixed-only talents cannot be learned by random cats");
            }
            check(families.size()<=5,"at most five talents");
        }
    }
    const auto again=recommendRealFleets(db,assessor,inventory,{team,low});
    check(again[0].commander.cat.instanceId==plan[0].commander.cat.instanceId,"deterministic");
    check(assessEmbryo(db,inventory.front()).tier==before.tier,"plans cannot change embryo grade");
    check(recommendRealFleets(db,assessor,{inventory.front()},{team})[0].complete==false,"shortage honest");
    FleetTeam empty=team;empty.ships.clear();
    check(!recommendRealFleets(db,assessor,inventory,{empty})[0].complete,"empty team omitted");
    FleetTeam bbv=team;bbv.ships={{"bbv","航战","重樱","航战",0}};
    check(fleetTalentValue(db.requireTalent("special_one_shot"),bbv).value==0,"one-shot excludes BBV");
    check(fleetTalentValue(db.requireTalent("aviation_special_3"),bbv).value>0,"special aviation applies to BBV");
    check(fleetTalentValue(db.requireTalent("aviation_carrier_3"),low).beneficiaries.size()==1,"ordinary aviation includes CVL");
    FleetTeam high=team;high.id="higher";high.priority=1;
    FleetTeam lower=team;lower.id="lower";lower.priority=9;
    const auto priorities=recommendRealFleets(db,assessor,inventory,{lower,high});
    check(priorities[0].complete&&priorities[1].complete,"priority cannot remove coverage");
    auto coverageInventory=inventory;
    coverageInventory[0].catId="oscar";coverageInventory[2].catId="oscar";
    const auto coverage=recommendRealFleets(db,assessor,coverageInventory,{low,low});
    check(coverage[0].complete&&coverage[1].complete,"reserve rare species for remaining teams");
    std::vector<InventoryCat> catalog;
    for(const auto& species:db.species()){InventoryCat cat;cat.catId=species.catId;cat.instanceId="theory_"+species.catId;catalog.push_back(cat);}
    const auto theory=recommendRealFleets(db,assessor,catalog,{team,team},true);
    check(theory[0].complete&&theory[1].complete,"theoretical result independent of inventory");
    check(theory[0].commander.cat.catId==theory[1].commander.cat.catId,"theory permits separate instances of same species across teams");
    check(theory[0].commander.route.targetTalentIds.size()==5,"theory has full ideal talents");
    auto primary=team;primary.planId="opsi";primary.priority=1;
    auto secondary=low;secondary.planId="campaign";secondary.priority=2;
    std::vector<InventoryCat> shared={inventory[0],inventory[1]};
    const auto unified=recommendRealFleets(db,assessor,shared,{secondary,primary});
    check(unified[0].complete&&unified[1].complete,"cross-plan instance reuse permitted");
    std::map<std::string,std::vector<std::string>> routes;
    for(const auto* m:{&unified[1].commander,&unified[1].staff})routes[m->cat.instanceId]=m->route.targetTalentIds;
    for(const auto* m:{&unified[0].commander,&unified[0].staff}){
        auto target=routes.at(m->cat.instanceId);auto actual=m->route.targetTalentIds;
        std::sort(target.begin(),target.end());std::sort(actual.begin(),actual.end());
        check(target==actual,"lower priority must use higher priority route");
        double value=0;for(const auto& id:actual)value+=fleetTalentValue(db.requireTalent(id),secondary).value;
        check(std::abs(value-m->talentValue)<1e-9,"rescore fixed route against lower team's ships");
    }
    secondary.priority=1;bool conflict=false;
    try{recommendRealFleets(db,assessor,shared,{primary,secondary});}catch(const std::runtime_error& e){conflict=std::string(e.what()).find("同优先级")!=std::string::npos;}
    check(conflict,"equal priority incompatible directions ask instead of guessing");
    std::cout<<"Real fleet tests passed\n";
}
