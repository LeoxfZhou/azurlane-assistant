#pragma once
#include "commander_cat_assessment.hpp"
#include <vector>

struct FleetShip {
    std::string id, name, faction, type;
    int slot = 0;
};
struct FleetTeam {
    std::string id, name, kind, goal, mode;
    std::vector<FleetShip> ships;
    int priority = 1;
    std::string planId = {};
};
struct FleetTalentValue {
    double value = 0;
    std::vector<std::string> beneficiaries;
};
struct FleetCatChoice {
    InventoryCat cat;
    RouteAssessment route;
    double talentValue = 0, skillValue = 0;
    std::vector<std::string> reasons;
};
struct FleetTeamResult {
    FleetTeam team;
    bool complete = false;
    FleetCatChoice commander, staff;
    std::string warning;
};

// Utility points, never an actual damage/probability estimate.
FleetTalentValue fleetTalentValue(const TalentDefinition& talent, const FleetTeam& team);
// Confirmed skill coverage only; utility points, not damage or skill magnitude.
FleetTalentValue fleetSkillValue(const std::string& catId, const FleetTeam& team,
    CatSlot slot, std::vector<std::string>& reasons);
std::vector<FleetTeamResult> recommendRealFleets(const CommanderCatDatabase& database,
    const CatAssessor& assessor, const std::vector<InventoryCat>& inventory,
    const std::vector<FleetTeam>& teams, bool theoretical = false,
    const std::map<std::string,std::vector<std::string>>& fixedTargets = {});
