#include "commander_cat_embryo.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace {
struct UseDefinition {
    std::string name;
    std::string prefix;
    bool specialist;
    std::map<std::string, double> specials;
    std::map<std::string, double> normals;
};

std::vector<UseDefinition> definitions() {
    const std::map<std::string, double> surface = {
        {"special_fire", 2}, {"special_fate", 1},
        {"special_fight_on_sight", 1}};
    std::vector<UseDefinition> result;
    for (bool carrier : {false, true}) {
        for (bool defense : {false, true}) {
            UseDefinition d;
            d.name = carrier ? (defense ? "正航＋巡洋·自动生存" : "正航＋巡洋·自动输出")
                             : (defense ? "战列＋巡洋·自动生存" : "战列＋巡洋·自动输出");
            d.prefix = carrier ? (defense ? "航巡兼辅" : "航巡兼伤")
                               : (defense ? "战巡兼辅" : "战巡兼伤");
            d.specialist = false;
            d.specials = surface;
            d.specials[carrier ? "special_ace_pilot" : "special_one_shot"] = 1.5;
            d.normals = {{"command_cruiser", 1}, {"gunnery_cruiser", .75},
                         {"torpedo_cruiser", .5}, {"reload_cruiser", .5},
                         {"command_destroyer",1},{"gunnery_destroyer",.75},{"torpedo_destroyer",.5}};
            if (carrier) {
                d.normals["command_carrier"] = 1;
                d.normals["aviation_carrier"] = 1;
                d.normals["maintenance"] = .5;
            } else {
                d.normals["command_battleship"] = 1;
                d.normals["gunnery_main"] = 1;
                d.normals["reload_battleship"] = .5;
            }
            // Soul is useful in an automatic light-cruiser formation, but is
            // not promoted to a premium rainbow merely because it is special.
            d.normals["special_torpedo_soul"] = 1;
            if (defense) {
                d.specials["special_fate"] = .5;
                d.specials["special_mountain"] = 1.5;
                d.specials["special_forest"] = 1.5;
                d.specials["special_miracle"] = 1;
                d.normals["helm_medium"] = 1;
                d.normals["engine_cruiser"] = .75;
                d.normals["aa_vanguard"] = .5;
                d.normals["aa_main"] = .5;
            }
            result.push_back(d);
        }
    }
    result.push_back({"潜艇·输出", "潜兼伤", false,
        {{"special_fire", 2}, {"special_wolf_pack", 1.5}},
        {{"command_submarine", 1}, {"torpedo_submarine", 1},
         {"reload_submarine", .75}, {"special_fate", .75}}});
    result.push_back({"轻航低耗＋驱逐·自动", "突兼辅", false,
        {{"special_fire", 2}, {"special_fate", 1}, {"special_mountain", 1},
         {"special_forest", 1}, {"special_miracle", 1}},
        {{"maintenance", .5}, {"command_destroyer", 1},
         {"gunnery_destroyer", .75}, {"torpedo_destroyer", .5},
         {"helm_small", 1}, {"engine_destroyer", .75}, {"special_torpedo_soul", 1}}});
    result.push_back({"驱逐／轻巡雷击·专项保留", "雷兼伤", true,
        {{"special_fire", 2}, {"special_fate", 1}, {"special_torpedo_soul", 1.5}},
        {{"torpedo_destroyer", 1}, {"command_destroyer", 1},
         {"reload_destroyer", .5}}});
    result.push_back({"轻巡雷击·专项保留", "轻兼伤", true,
        {{"special_fire", 2}, {"special_fate", 1}, {"special_torpedo_soul", 1.5}},
        {{"torpedo_cruiser", 1}, {"command_cruiser", 1}, {"reload_cruiser", .5}}});
    result.push_back({"水面速刷·航速专项", "速兼辅", true,
        {{"special_fire",2},{"special_wind",1.5},{"special_fate",1}},
        {{"command_destroyer",1},{"command_cruiser",1},{"gunnery_destroyer",.75},
         {"gunnery_cruiser",.75},{"engine_destroyer",.5},{"engine_cruiser",.5}}});
    result.push_back({"水面防潜·专项", "潜防兼辅", true,
        {{"special_fire",2},{"special_forest",1.5},{"special_mountain",1}},
        {{"sonar_vanguard",1},{"sonar_main",1},{"maintenance",.5},
         {"command_destroyer",1},{"command_cruiser",1}}});
    result.push_back({"轻航／维修／重炮·辅助", "辅兼辅", true,
        {{"special_fire",2},{"special_mountain",1.5},{"special_forest",1}},
        {{"maintenance",1},{"aviation_special",1},{"reload_special",1},
         {"engine_special",1},{"helm_large",1},{"aa_main",.75}}});
    for(auto& use:result) {
        const std::string name=use.name;
        if(name=="潜艇·输出") {
            use.normals["observation_submarine"]=.75;
            use.normals["helm_small"]=.5;
            use.normals["engine_submarine"]=.5;
            use.normals["engine_small"]=.5;
        } else {
            use.normals["observation_vanguard"]=.75;
            use.normals["observation_main"]=.75;
            use.normals["aa_main"]=.5;use.normals["aa_vanguard"]=.5;
            if(name.find("生存")!=std::string::npos) {
                use.normals["engine_battleship"]=.75;
                use.normals["engine_carrier"]=.75;
                use.normals["helm_large"]=1;
            }
        }
    }
    // Each role can have a mono-faction variant; never restrict faction value
    // to battleships while dropping equally valid carrier/submarine uses.
    const auto generic=result;
    for(const auto& faction:std::map<std::string,std::string>{{"command_faction_eagle","白鹰"},
        {"command_faction_royal","皇家"},{"command_faction_sakura","重樱"},{"command_faction_ironblood","铁血"}})
        for(auto use:generic) {
            use.name=faction.second+"阵营·"+use.name;use.specialist=true;
            use.normals[faction.first]=1;result.push_back(std::move(use));
        }
    return result;
}

int tierOrder(const std::string& tier) {
    if (tier == "S") return 5;
    if (tier == "A") return 4;
    if (tier == "B") return 3;
    if (tier == "C") return 2;
    return 1;
}

std::string tierFor(const EmbryoUse& use) {
    const double x = use.specialValue, y = use.normalPoints;
    // Deliberately conservative: one premium talent plus junk is not an S.
    if ((use.premiumCount >= 2 && x >= 3 && y >= 1) ||
        (use.premiumCount >= 1 && x >= 1.5 && y >= 3)) return "S";
    if ((x >= 2.5 && use.premiumCount >= 1) || (x >= 1.5 && y >= 2) ||
        (x >= 1 && y >= 3)) return "A";
    if ((x >= 1 && y >= 1) || x >= 2 || y >= 3) return "B";
    if (x > 0 || y >= 1) return "C";
    return "备";
}
}

EmbryoAssessment assessEmbryo(const CommanderCatDatabase& database,
                             const InventoryCat& cat) {
    EmbryoAssessment result;
    const auto& species = database.requireSpecies(cat.catId);
    if (species.fixedRoute) {
        result.fixed = true;
        result.tier = "固定";
        result.nicknamePrefix = "定兼辅";
        result.explanation = "固定成长，不参加随机初始天赋分级。";
        return result;
    }
    std::map<std::string, const TalentDefinition*> initial;
    for (const auto& id : cat.initialTalentIds) {
        const auto& t = database.requireTalent(id);
        if (t.kind == TalentKind::Placeholder || t.kind == TalentKind::Fixed ||
            t.level < 1 || t.level > 2 || initial.count(t.familyId)) {
            result.explanation = "初始天赋存在占位、固定天赋、重复成长链或非初始等级，不能据此淘汰。";
            return result;
        }
        initial[t.familyId] = &t;
    }
    if (initial.empty()) {
        result.explanation = "没有初始天赋，不能据此淘汰。";
        return result;
    }
    int ones = 0, twos = 0;
    for (const auto& entry : initial) {
        if (entry.second->level == 2 && entry.second->kind == TalentKind::Normal) ++twos;
        else ++ones;
    }
    const bool valid = species.rarity == "SSR"
        ? ((ones == 3 && twos == 0) || (ones == 2 && twos == 1) || (ones == 0 && twos == 2))
        : species.rarity == "SR" ? ((ones == 2 && twos == 0) || (ones == 0 && twos == 1))
        : species.rarity == "R" && ones == 1 && twos == 0;
    if (!valid) {
        result.explanation = "初始天赋数量／等级不符合稀有度结构，请核对扫描；不能据此淘汰。";
        return result;
    }
    const std::set<std::string> premium = {"special_fire", "special_one_shot",
        "special_ace_pilot", "special_wolf_pack", "special_torpedo_soul"};
    bool uncovered = false;
    std::set<std::string> covered;
    for (const auto& d : definitions()) {
        EmbryoUse use;
        use.name = d.name;
        use.nicknamePrefix = d.prefix;
        use.specialist = d.specialist;
        for (const auto& [family, t] : initial) {
            EmbryoContribution contribution;
            contribution.talentId = t->talentId;
            if (const auto found = d.specials.find(family); found != d.specials.end()) {
                contribution.specialValue = found->second;
                if (premium.count(family) && found->second >= 1.5) ++use.premiumCount;
                covered.insert(family);
            }
            if (const auto found = d.normals.find(family); found != d.normals.end()) {
                contribution.normalPoints = found->second * t->level;
                covered.insert(family);
            }
            use.specialValue += contribution.specialValue;
            use.normalPoints += contribution.normalPoints;
            use.contributions.push_back(contribution);
        }
        use.tier = tierFor(use);
        result.uses.push_back(use);
    }
    // Unknown/utility/faction families are not equated to trash. Known XP
    // talents have no combat contribution; missing them never incurs a penalty.
    for (const auto& [family, t] : initial) {
        (void)t;
        if (!covered.count(family) && family != "special_new_star" &&
            family != "special_best_friend") uncovered = true;
    }
    std::stable_sort(result.uses.begin(), result.uses.end(), [](const auto& a, const auto& b) {
        if (tierOrder(a.tier) != tierOrder(b.tier)) return tierOrder(a.tier) > tierOrder(b.tier);
        if (a.specialValue != b.specialValue) return a.specialValue > b.specialValue;
        return a.normalPoints > b.normalPoints;
    });
    const auto& best = result.uses.front();
    result.tier = best.tier;
    result.nicknamePrefix = best.nicknamePrefix;
    result.protectedRare = result.tier == "S" || (best.premiumCount>=2&&best.specialValue>=3);
    std::ostringstream explanation;
    explanation << best.name << "：有效特殊权重 " << best.specialValue
                << "，有效普通点 " << best.normalPoints
                << "。这是初始天赋筛选规则，不是概率、伤害或当前成品强度。";
    if (uncovered) {
        explanation << "含尚未完整覆盖的专项／阵营天赋，不建议仅凭此等级淘汰。";
        if (result.tier == "备") result.tier = "待评";
    }
    if (result.protectedRare) explanation << "极品保留，不受当前舰队数量影响。";
    result.explanation = explanation.str();
    return result;
}
