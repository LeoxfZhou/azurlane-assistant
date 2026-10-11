#pragma once

#include "commander_cat_model.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CommanderCatDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CommanderCatDatabase {
public:
    static CommanderCatDatabase loadFromDirectory(
        const std::filesystem::path& directory);

    const std::vector<CommanderCatSpecies>& species() const noexcept;
    const std::vector<TalentDefinition>& talents() const noexcept;
    const std::vector<BuildPlan>& builds() const noexcept;
    const std::vector<RankingRecord>& rankings() const noexcept;
    const std::vector<FixedRouteNode>& fixedRoutes() const noexcept;
    const std::vector<FixedScenarioRecommendation>& fixedRecommendations() const noexcept;
    const std::vector<GuideRecommendation>& guideRecommendations() const noexcept;

    const CommanderCatSpecies& requireSpecies(const std::string& catId) const;
    const TalentDefinition& requireTalent(const std::string& talentId) const;
    const TalentDefinition* findTalentByName(const std::string& nameOrAlias) const;
    std::vector<const CommanderCatSpecies*> findSpecies(const std::string& text) const;
    std::vector<const TalentDefinition*> findTalentFamilies(const std::string& text) const;
    std::vector<const BuildPlan*> buildsFor(const std::string& catId) const;
    std::vector<const RankingRecord*> rankingsFor(const std::string& catId,
                                                   const std::string& category) const;
    std::vector<const FixedRouteNode*> fixedRouteFor(const std::string& catId) const;
    std::vector<const FixedScenarioRecommendation*> fixedRecommendationsFor(
        const std::string& category) const;
    std::vector<const GuideRecommendation*> guideRecommendationsFor(
        const std::string& catId) const;
    bool nicknameAllowed(const std::string& nickname) const;
    const std::unordered_set<std::string>& initialPoolFor(
        const std::string& catId) const;

private:
    std::vector<CommanderCatSpecies> species_;
    std::vector<TalentDefinition> talents_;
    std::vector<BuildPlan> builds_;
    std::vector<RankingRecord> rankings_;
    std::vector<FixedRouteNode> fixedRoutes_;
    std::vector<FixedScenarioRecommendation> fixedRecommendations_;
    std::vector<GuideRecommendation> guideRecommendations_;
    std::vector<std::string> nicknameBlockedTokens_;
    std::unordered_map<std::string, std::size_t> speciesById_;
    std::unordered_map<std::string, std::size_t> talentById_;
    std::unordered_map<std::string, std::size_t> talentByName_;
    std::unordered_map<std::string, std::unordered_set<std::string>> initialPools_;
};

std::vector<TalentChoiceGroup> parseTalentExpression(
    const std::string& expression,
    const CommanderCatDatabase& database,
    const std::string& context);
