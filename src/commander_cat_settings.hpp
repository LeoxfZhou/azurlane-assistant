#pragma once

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

class CommanderCatSettingsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CommanderCatSettings {
public:
    explicit CommanderCatSettings(std::filesystem::path filePath);

    void load();
    void save() const;
    void setFleetCounts(const std::map<std::string, int>& counts);

    int fleetCount(const std::string& category) const;
    const std::map<std::string, int>& fleetCounts() const noexcept;
    const std::string& warning() const noexcept;
    const std::filesystem::path& filePath() const noexcept;

    static const std::vector<std::string>& categories();

private:
    static std::map<std::string, int> defaults();
    static void validate(const std::map<std::string, int>& counts);

    std::filesystem::path filePath_;
    std::map<std::string, int> fleetCounts_;
    std::string warning_;
};
