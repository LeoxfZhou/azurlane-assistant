#pragma once

#include "commander_cat_model.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

class CommanderCatInventoryError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CommanderCatInventory {
public:
    explicit CommanderCatInventory(std::filesystem::path filePath);

    void load();
    void save() const;
    const std::vector<InventoryCat>& cats() const noexcept;
    InventoryCat& add(const std::string& catId,
                      std::string nickname,
                      std::vector<std::string> initialTalentIds,
                      std::optional<double> potentialScore = std::nullopt,
                      std::string potentialTier = {});
    void update(const std::string& instanceId,
                const std::string& catId,
                std::string nickname,
                std::vector<std::string> initialTalentIds,
                std::optional<double> potentialScore = std::nullopt,
                std::string potentialTier = {});
    void updateMetadata(const std::string& instanceId,
                        std::string nickname,
                        std::optional<double> potentialScore,
                        std::string potentialTier,
                        bool saveImmediately = true);
    void remove(const std::string& instanceId);
    const InventoryCat* find(const std::string& instanceId) const noexcept;
    const std::filesystem::path& filePath() const noexcept;

private:
    std::string nextInstanceId() const;
    std::filesystem::path filePath_;
    std::vector<InventoryCat> cats_;
};
