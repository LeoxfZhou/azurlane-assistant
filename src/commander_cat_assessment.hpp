#pragma once

#include "commander_cat_database.hpp"
#include "commander_cat_model.hpp"

#include <cstddef>
#include <map>
#include <string>

class CatAssessor {
public:
    explicit CatAssessor(const CommanderCatDatabase& database);
    AssessmentResult assess(const InventoryCat& cat) const;
    PotentialAssessment assessPotential(const InventoryCat& cat) const;
    RouteAssessment assessRankingRoute(const InventoryCat& cat,
                                       const RankingRecord& ranking) const;
    std::string automaticNickname(const RouteAssessment& route) const;
    std::string nicknameBase(const PotentialAssessment& potential) const;
    std::string uniqueNickname(const PotentialAssessment& potential,
                               const std::vector<InventoryCat>& inventory,
                               const std::string& excludedInstance = {}) const;

private:
    RouteAssessment assessTarget(const InventoryCat& cat,
                                 const std::vector<TalentChoiceGroup>& groups,
                                 const std::string& buildId,
                                 const std::string& category,
                                 const std::string& position,
                                 const std::string& style,
                                 const std::string& difficultyFloor,
                                 const RankingRecord* ranking,
                                 const std::string& notes,
                                 const std::string& evaluation) const;
    const RankingRecord* bestRanking(const BuildPlan& plan) const;
    const CommanderCatDatabase& database_;
};

class CatPairRecommender {
public:
    CatPairRecommender(const CommanderCatDatabase& database,
                       const CatAssessor& assessor);
    PairRecommendation recommend(const std::string& category,
                                 const std::vector<InventoryCat>& inventory,
                                 std::size_t maxStableChoices = 3) const;

private:
    const CommanderCatDatabase& database_;
    const CatAssessor& assessor_;
};

class CatFleetPlanner {
public:
    explicit CatFleetPlanner(const CatPairRecommender& recommender);
    InventoryFleetPlan plan(
        const std::vector<InventoryCat>& inventory,
        const std::map<std::string, int>& requestedCounts) const;

private:
    const CatPairRecommender& recommender_;
};

class CatGraduationRecommender {
public:
    explicit CatGraduationRecommender(const CommanderCatDatabase& database);
    GraduationRecommendation recommend(const std::string& category) const;

private:
    const CommanderCatDatabase& database_;
};
