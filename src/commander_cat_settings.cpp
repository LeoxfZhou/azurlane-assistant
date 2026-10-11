#include "commander_cat_settings.hpp"
#include "commander_cat_atomic.hpp"

#include <fstream>
#include <set>

namespace {

int parseCount(const std::string& text) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size()) throw std::invalid_argument("trailing");
        return value;
    } catch (...) {
        throw CommanderCatSettingsError("舰队数量不是整数：" + text);
    }
}

}  // namespace

CommanderCatSettings::CommanderCatSettings(std::filesystem::path filePath)
    : filePath_(std::move(filePath)), fleetCounts_(defaults()) {}

const std::vector<std::string>& CommanderCatSettings::categories() {
    static const std::vector<std::string> value = {
        "战列", "航母", "潜艇", "重巡", "轻巡", "驱逐", "突击者"};
    return value;
}

std::map<std::string, int> CommanderCatSettings::defaults() {
    std::map<std::string, int> result;
    for (const auto& category : categories()) result[category] = 1;
    return result;
}

void CommanderCatSettings::validate(const std::map<std::string, int>& counts) {
    if (counts.size() != categories().size())
        throw CommanderCatSettingsError("设置文件必须包含全部七类舰队");
    for (const auto& category : categories()) {
        const auto found = counts.find(category);
        if (found == counts.end())
            throw CommanderCatSettingsError("设置文件缺少舰队类别：" + category);
        if (found->second < 0 || found->second > 3)
            throw CommanderCatSettingsError(category + "舰队数量必须是 0—3");
    }
}

void CommanderCatSettings::load() {
    fleetCounts_ = defaults();
    warning_.clear();
    if (!std::filesystem::exists(filePath_)) return;
    try {
        std::ifstream input(filePath_);
        if (!input) throw CommanderCatSettingsError("无法打开设置文件");
        std::string line;
        if (!std::getline(input, line)) throw CommanderCatSettingsError("设置文件为空");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "category,fleet_count")
            throw CommanderCatSettingsError("设置文件表头损坏");
        std::map<std::string, int> loaded;
        std::size_t number = 1;
        while (std::getline(input, line)) {
            ++number;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            const auto comma = line.find(',');
            if (comma == std::string::npos || line.find(',', comma + 1) != std::string::npos)
                throw CommanderCatSettingsError("设置文件第 " + std::to_string(number) + " 行格式错误");
            const auto category = line.substr(0, comma);
            if (loaded.count(category))
                throw CommanderCatSettingsError("设置文件类别重复：" + category);
            loaded[category] = parseCount(line.substr(comma + 1));
        }
        validate(loaded);
        fleetCounts_ = std::move(loaded);
    } catch (const std::exception& error) {
        warning_ = std::string("设置文件损坏，当前临时使用七类各 1 队；原文件未被覆盖：") + error.what();
        fleetCounts_ = defaults();
    }
}

void CommanderCatSettings::save() const {
    validate(fleetCounts_);
    std::error_code error;
    const auto parent = filePath_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    if (error) throw CommanderCatSettingsError("无法创建设置目录：" + error.message());
    auto temporary = filePath_;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw CommanderCatSettingsError("无法写入临时设置文件");
        output << "category,fleet_count\n";
        for (const auto& category : categories())
            output << category << ',' << fleetCounts_.at(category) << '\n';
        output.flush();
        if (!output) throw CommanderCatSettingsError("写入临时设置文件失败");
    }
    try { replaceCatFileWithBackup(temporary, filePath_); }
    catch (const std::exception& failure) {
        throw CommanderCatSettingsError(std::string("原子替换设置失败：") + failure.what());
    }
}

void CommanderCatSettings::setFleetCounts(const std::map<std::string, int>& counts) {
    validate(counts);
    fleetCounts_ = counts;
    save();
    warning_.clear();
}

int CommanderCatSettings::fleetCount(const std::string& category) const {
    const auto found = fleetCounts_.find(category);
    if (found == fleetCounts_.end()) throw CommanderCatSettingsError("未知舰队类别：" + category);
    return found->second;
}

const std::map<std::string, int>& CommanderCatSettings::fleetCounts() const noexcept {
    return fleetCounts_;
}

const std::string& CommanderCatSettings::warning() const noexcept { return warning_; }
const std::filesystem::path& CommanderCatSettings::filePath() const noexcept { return filePath_; }
