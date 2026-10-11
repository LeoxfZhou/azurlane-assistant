#include "commander_cat_embryo.hpp"
#include <iostream>
#include <stdexcept>
#include <sstream>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    const auto db = CommanderCatDatabase::loadFromDirectory(AZUR_LANE_COMMANDER_CAT_DATA_DIR);
    const auto grade = [&](std::vector<std::string> ids, std::string species = "oscar") {
        return assessEmbryo(db, {"TEST", species, "", ids, std::nullopt, {}});
    };
    if(argc==3) {
        std::istringstream input(argv[2]);std::string id;std::vector<std::string> ids;
        while(std::getline(input,id,','))if(!id.empty())ids.push_back(id);
        const auto value=grade(ids,argv[1]);
        std::cout<<value.tier<<'\t'<<(value.protectedRare?"true":"false")<<'\n';return 0;
    }
    const auto rare = grade({"special_fire", "special_one_shot", "gunnery_main_1"});
    check(rare.tier == "S" && rare.protectedRare, "synergistic premium embryo must be protected S");
    const auto mixed = grade({"special_fire", "command_cruiser_2", "gunnery_main_1"});
    check(mixed.tier == "S", "mixed main/vanguard effective points must count together");
    const auto soul = grade({"special_torpedo_soul", "special_fate", "reload_battleship_1"}, "takemaru");
    check(soul.tier != "备", "soul remains useful in automatic and specialist contexts");
    bool automaticSoul = false, submarineSoul = false;
    for (const auto& use : soul.uses) for (const auto& c : use.contributions) {
        if(c.talentId != "special_torpedo_soul") continue;
        if(use.name == "战列＋巡洋·自动输出") automaticSoul = c.normalPoints > 0;
        if(use.name == "潜艇·输出") submarineSoul = c.specialValue > 0 || c.normalPoints > 0;
    }
    check(automaticSoul && !submarineSoul, "soul must not leak to submarines");
    check(grade({"special_fire", "special_new_star", "gunnery_main_1"}).tier ==
          grade({"special_fire", "special_best_friend", "gunnery_main_1"}).tier,
          "irrelevant XP talents must not incur different missing penalties");
    check(grade({"special_wind"}, "lady").tier == "C", "speed utility gets a specialist assessment, never trash");
    check(grade({"special_new_star"}, "lady").tier == "备", "XP-only has no combat value");
    check(grade({"special_fire"}).tier == "待评", "incomplete SSR scan cannot be graded");
    check(grade({"gunnery_main_1", "gunnery_main_2"}).tier == "待评", "duplicate family invalid");
    check(grade({"random_1"}).tier == "待评", "placeholder cannot be graded");
    check(grade({"gunnery_main_3"}).tier == "待评", "acquired max talent is not initial");
    check(grade({}, "drake").fixed, "fixed growth separate");
    const auto twoPremium=grade({"special_fire","special_one_shot","special_new_star"});
    check(twoPremium.tier=="A"&&twoPremium.protectedRare,"double premium A remains protected");
    check(grade({"special_ace_pilot","command_faction_sakura_2","special_new_star"},"bishamaru").tier=="A","faction carrier uses not lost");
    for(const auto& species:db.species()) if(!species.fixedRoute) {
        for(const auto& id:db.initialPoolFor(species.catId)) {
            const auto& t=db.requireTalent(id);
            if(t.kind!=TalentKind::Normal||t.level!=1)continue;
            const auto result=grade({id,"special_new_star","special_best_friend"});
            check(result.explanation.find("尚未完整覆盖")==std::string::npos,"observed initial normal family has no defined use");
        }
    }
    // Read-only audit mode: CSV is only read; inventory loader is intentionally
    // not used because it may migrate legacy files on load.
    std::cout << "Embryo rules " << CAT_EMBRYO_RULE_VERSION << " passed\n";
}
