#pragma once

#include "commander_cat_database.hpp"

// Versioned, explainable screening policy. These are utility weights, not drop rates.
inline constexpr const char* CAT_EMBRYO_RULE_VERSION = "embryo-2026-10-06-v2";

struct EmbryoContribution {
    std::string talentId;
    double specialValue = 0;
    double normalPoints = 0;
};

struct EmbryoUse {
    std::string name;
    std::string nicknamePrefix;
    std::string tier;
    double specialValue = 0;
    double normalPoints = 0;
    int premiumCount = 0;
    bool specialist = false;
    std::vector<EmbryoContribution> contributions;
};

struct EmbryoAssessment {
    std::string tier = "待评";
    std::string nicknamePrefix = "待评兼辅";
    bool protectedRare = false;
    bool fixed = false;
    std::vector<EmbryoUse> uses;
    std::string explanation;
};

// No inventory, team counts or acquired talents enter this assessment.
EmbryoAssessment assessEmbryo(const CommanderCatDatabase& database,
                             const InventoryCat& cat);
