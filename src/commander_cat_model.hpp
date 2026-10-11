#pragma once

#include <optional>
#include <map>
#include <string>
#include <vector>

enum class TalentKind { Normal, Special, Fixed, Placeholder };
enum class CatSlot { Commander, Staff, Either };
enum class CatDifficulty { Easy, Medium, Hard, VeryHard, Unreachable };

struct CommanderCatSpecies {
    std::string catId;
    std::string name;
    std::string englishName;
    std::string faction;
    std::string rarity;
    std::string skillLv1Bonus;
    std::string skillLv2Bonus;
    std::string skillLv3Bonus;
    std::string review;
    std::string sourceImages;
    bool fixedRoute = false;
};

struct TalentDefinition {
    std::string talentId;
    std::string familyId;
    std::string name;
    int level = 1;
    TalentKind kind = TalentKind::Normal;
    std::string effect;
    std::vector<std::string> aliases;
};

using TalentChoiceGroup = std::vector<std::string>;

struct BuildPlan {
    std::string buildId;
    std::string category;
    std::string catId;
    int variantOrder = 0;
    std::string initialTalentsText;
    std::string targetTalentsText;
    std::vector<TalentChoiceGroup> targetTalentGroups;
    std::string position;
    std::string buildStyle;
    std::string notes;
    std::string evaluation;
    std::string sourceImage;
    std::string difficultyFloor;
};

struct RankingRecord {
    std::string category;
    int rank = 0;
    std::string catId;
    std::string displayLabel;
    std::string faction;
    std::string rarity;
    std::string position;
    CatSlot slot = CatSlot::Either;
    std::string buildStyle;
    std::string scenario;
    bool stable = true;
    std::string targetTalentsText;
    std::vector<TalentChoiceGroup> targetTalentGroups;
    double k1 = 0.0;
    double k2 = 0.0;
    double kPercent = 0.0;
    std::string sourceImage;
};

struct FixedRouteNode {
    std::string catId;
    int level = 0;
    std::vector<std::string> talentIds;
    std::string description;
};

struct FixedScenarioRecommendation {
    std::string recommendationId;
    std::string category;
    std::string catId;
    CatSlot slot = CatSlot::Either;
    std::string scenario;
    std::string reason;
};

struct GuideRecommendation {
    std::string guideId;
    std::string catId;
    std::string scenario;
    CatSlot slot = CatSlot::Either;
    std::string initialSpecials;
    std::string initialNormals;
    std::string requiredTalents;
    std::string optionalTalents;
    std::string recommendation;
    std::string notes;
    bool covered = true;
};

struct InventoryCat {
    std::string instanceId;
    std::string catId;
    std::string nickname;
    std::vector<std::string> initialTalentIds;
    // 旧库存中的派生评分仅为兼容读取；保存时不再写回。
    std::optional<double> potentialScore;
    std::string potentialTier;
};

struct TalentUpgradeAction {
    // 包含当前/新学的起点以及最终目标，便于显示完整成长链。
    std::vector<std::string> pathTalentIds;
    int requiredPoints = 0;
};

struct RouteAssessment {
    std::string buildId;
    std::string category;
    std::string position;
    std::string buildStyle;
    CatSlot slot = CatSlot::Either;
    std::vector<std::string> targetTalentIds;
    std::vector<std::string> satisfiedTalentIds;
    std::vector<std::string> missingTalentIds;
    std::vector<std::string> upgradeTalentIds;
    // 培养操作与最终目标分开保存，避免把待覆盖的废天赋误写成“后续优先”。
    std::vector<std::string> retainedTalentIds;
    std::vector<std::string> acquireTalentIds;
    std::vector<TalentUpgradeAction> upgradeActions;
    std::vector<std::string> replacementTalentIds;
    std::vector<std::string> flexibleInitialTalentIds;
    std::vector<std::string> priorityTalentIds;
    std::vector<std::string> unrelatedInitialTalentIds;
    int requiredPoints = 0;
    int missingSpecialCount = 0;
    CatDifficulty difficulty = CatDifficulty::Unreachable;
    bool reachable = false;
    bool stable = true;
    bool hasRanking = false;
    int rank = 0;
    int stableRank = 0;
    int categorySize = 0;
    double k1 = 0.0;
    double k2 = 0.0;
    double kPercent = 0.0;
    std::string strengthTier = "U";
    std::string scenario;
    std::string notes;
    std::string evaluation;
    int newNormalFamilyCount = 0;
    int overwriteCount = 0;
    double feasibilityScore = 0.0;
    std::string feasibilityTier = "C";
    bool ceilingQuantified = false;
    double ceilingScore = 0.0;
    std::string ceilingTier = "U";
    bool nearGraduationStart = false;
    std::string guideScenario;
    std::string guideRecommendation;
};

struct AssessmentResult {
    std::string instanceId;
    std::string catId;
    std::vector<RouteAssessment> routes;
    std::optional<std::size_t> mainRoute;
    std::vector<std::size_t> alternatives;
    bool fixedRoute = false;
};

struct PotentialAssessment {
    std::string embryoNicknamePrefix;
    std::string embryoExplanation;
    bool protectedRare = false;
    bool hasRoute = false;
    bool quantified = false;
    double score = 0.0;
    std::string tier = "U";
    RouteAssessment route;
    bool fixedGrowth = false;
    bool ceilingQuantified = false;
    double ceilingScore = 0.0;
    std::string ceilingTier = "U";
    bool nearGraduationStart = false;
    RouteAssessment finalRoute;
    RouteAssessment practicalRoute;
    bool routesDiffer = false;
};

struct PairChoice {
    InventoryCat commander;
    InventoryCat staff;
    RouteAssessment commanderRoute;
    RouteAssessment staffRoute;
    double totalKPercent = 0.0;
    bool hasComparableKPercent = false;
    double commanderFit = 0.0;
    double staffFit = 0.0;
    double pairFit = 0.0;
    int fallbackLevel = 0;
    std::string commanderFitLabel;
    std::string staffFitLabel;
    std::string conditions;
};

struct TheoreticalPair {
    std::string commanderCatId;
    std::string staffCatId;
    double totalKPercent = 0.0;
    std::vector<std::string> missingCatIds;
};

struct PairRecommendation {
    std::string category;
    std::vector<PairChoice> stableChoices;
    std::optional<PairChoice> conditionalCeiling;
    std::optional<TheoreticalPair> theoreticalBest;
    std::vector<std::string> unrankedCandidates;
};

struct FleetCategoryPlan {
    std::string category;
    std::size_t requested = 0;
    std::vector<PairChoice> choices;
};

struct InventoryFleetPlan {
    std::vector<FleetCategoryPlan> categories;
    std::vector<std::string> usedInstanceIds;
};

struct GraduationPair {
    RankingRecord commander;
    RankingRecord staff;
    double totalKPercent = 0.0;
    std::string conditions;
};

struct FixedScenarioPair {
    FixedScenarioRecommendation recommendation;
    CatSlot assignedSlot = CatSlot::Either;
    RankingRecord partner;
    std::vector<std::string> fixedTargetTalentIds;
};

struct GraduationRecommendation {
    std::string category;
    std::optional<GraduationPair> generalBest;
    std::optional<GraduationPair> conditionalBest;
    std::vector<FixedScenarioPair> fixedScenarios;
};

std::string toText(TalentKind value);
std::string toText(CatSlot value);
std::string toText(CatDifficulty value);
