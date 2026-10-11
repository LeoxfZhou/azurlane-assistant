#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

enum class ShipStoryStatus { Unowned, InProgress, Completed };

std::string toText(ShipStoryStatus status);
std::optional<ShipStoryStatus> shipStoryStatusFromText(const std::string& text);

struct ShipStoryStep {
    int order = 0;
    std::string title;
    std::string requirement;
    std::string reward;
};

struct ShipStory {
    std::string shipId;
    std::string wikiName;
    std::string canonicalName;
    std::string harmonyName;
    std::vector<std::string> aliases;
    std::string camp;
    std::string shipType;
    std::string storyTitle;
    std::string imageFile;
    std::vector<ShipStoryStep> steps;

    std::string displayLabel() const;
    std::string fleetGroup() const;
    bool matchesName(const std::string& query) const;
};

class ShipStoryDatabase {
public:
    static ShipStoryDatabase loadFromDirectory(const std::filesystem::path& directory);
    const std::vector<ShipStory>& stories() const noexcept;
    const ShipStory& require(const std::string& shipId) const;

private:
    std::vector<ShipStory> stories_;
};

struct ShipStoryProgressEntry {
    std::string shipId;
    ShipStoryStatus status = ShipStoryStatus::Unowned;
    bool isMaxLevel = false;
    std::string updatedAt;
};

class ShipStoryProgress {
public:
    ShipStoryProgress(std::filesystem::path file, const ShipStoryDatabase& database,
                      std::filesystem::path legacyShipsCsv);
    void load();
    void update(const std::string& shipId, ShipStoryStatus status,
                std::optional<bool> isMaxLevel = std::nullopt);
    const std::vector<ShipStoryProgressEntry>& entries() const noexcept;
    const ShipStoryProgressEntry& require(const std::string& shipId) const;
    void save() const;

private:
    void seedFromLegacy();
    std::filesystem::path file_;
    const ShipStoryDatabase& database_;
    std::filesystem::path legacyShipsCsv_;
    std::vector<ShipStoryProgressEntry> entries_;
    std::vector<ShipStoryProgressEntry> orphanEntries_;
};
