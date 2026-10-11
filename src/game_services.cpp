#include "game_services.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char value = line[i];
        if (value == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else quoted = !quoted;
        } else if (value == ',' && !quoted) {
            fields.push_back(trim(field));
            field.clear();
        } else field.push_back(value);
    }
    if (quoted) throw std::runtime_error("CSV contains an unterminated quote");
    fields.push_back(trim(field));
    return fields;
}

struct CsvTable {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
};

CsvTable readCsv(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open data file: " + path.string());
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("empty data file: " + path.string());
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef &&
        static_cast<unsigned char>(line[1]) == 0xbb &&
        static_cast<unsigned char>(line[2]) == 0xbf) line.erase(0, 3);
    CsvTable result;
    result.header = parseCsvLine(line);
    std::size_t lineNumber = 1;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (trim(line).empty()) continue;
        auto row = parseCsvLine(line);
        if (row.size() != result.header.size())
            throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber) +
                                     ": wrong column count");
        result.rows.push_back(std::move(row));
    }
    return result;
}

std::map<std::string, std::size_t> columns(const CsvTable& table,
                                           const std::vector<std::string>& required) {
    std::map<std::string, std::size_t> result;
    for (const auto& name : required) {
        const auto found = std::find(table.header.begin(), table.header.end(), name);
        if (found == table.header.end()) throw std::runtime_error("missing CSV column: " + name);
        result[name] = static_cast<std::size_t>(found - table.header.begin());
    }
    return result;
}

std::vector<std::string> split(const std::string& value, char separator = '|') {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, separator)) if (!trim(item).empty()) result.push_back(trim(item));
    return result;
}

bool parseBool(const std::string& value) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::runtime_error("boolean field must be true or false: " + value);
}

int parseInt(const std::string& value, const std::string& label) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size()) throw std::invalid_argument("trailing");
        return parsed;
    } catch (...) { throw std::runtime_error(label + " must be an integer: " + value); }
}

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string result = "\"";
    for (char c : value) result += c == '"' ? "\"\"" : std::string(1, c);
    return result + "\"";
}

std::string nowIso() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm value{};
#if defined(_WIN32)
    gmtime_s(&value, &now);
#else
    gmtime_r(&now, &value);
#endif
    std::ostringstream output;
    output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

void atomicWrite(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write temporary user data: " + temporary);
        output << content;
        output.flush();
        if (!output) throw std::runtime_error("failed while writing user data: " + temporary);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) throw std::runtime_error("cannot atomically replace user data: " + error.message());
    }
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsCaseInsensitive(const std::string& value, const std::string& query) {
    if (query.empty()) return true;
    return lowerAscii(value).find(lowerAscii(query)) != std::string::npos;
}

std::unordered_map<std::string, bool> legacyShips(const std::filesystem::path& path) {
    // Personal migration seeds are optional and never distributed publicly.
    if (!std::filesystem::exists(path)) return {};
    const auto table = readCsv(path);
    const auto c = columns(table, {"name", "alternate_name", "is_max_level"});
    std::unordered_map<std::string, bool> result;
    for (const auto& row : table.rows) {
        const bool max = parseBool(row[c.at("is_max_level")]);
        result[row[c.at("name")]] = max;
        if (!row[c.at("alternate_name")].empty()) result[row[c.at("alternate_name")]] = max;
    }
    return result;
}

bool isLegacyMatch(const ShipStory& story, const std::unordered_map<std::string, bool>& legacy,
                   bool& maxLevel) {
    for (const auto& value : [&]() {
             std::vector<std::string> values{
                 story.wikiName, story.canonicalName, story.harmonyName};
             values.insert(values.end(), story.aliases.begin(), story.aliases.end());
             return values;
         }()) {
        const auto found = legacy.find(value);
        if (found != legacy.end()) { maxLevel = found->second; return true; }
    }
    return false;
}

}  // namespace

std::string toText(ShipStoryStatus status) {
    if (status == ShipStoryStatus::Unowned) return "unowned";
    if (status == ShipStoryStatus::InProgress) return "in_progress";
    return "completed";
}

std::optional<ShipStoryStatus> shipStoryStatusFromText(const std::string& text) {
    if (text == "unowned") return ShipStoryStatus::Unowned;
    if (text == "in_progress") return ShipStoryStatus::InProgress;
    if (text == "completed") return ShipStoryStatus::Completed;
    return std::nullopt;
}

std::string ShipStory::displayLabel() const {
    if (harmonyName.empty() || harmonyName == canonicalName) return canonicalName;
    return canonicalName + "（" + harmonyName + "）";
}

std::string ShipStory::fleetGroup() const {
    static const std::set<std::string> vanguard = {"驱逐", "轻巡", "重巡", "超巡", "运输"};
    static const std::set<std::string> submarine = {"潜艇", "潜母"};
    if (vanguard.count(shipType)) return "前排";
    if (submarine.count(shipType)) return "潜艇";
    return "后排";
}

bool ShipStory::matchesName(const std::string& query) const {
    if (containsCaseInsensitive(canonicalName, query) ||
        containsCaseInsensitive(harmonyName, query) ||
        containsCaseInsensitive(wikiName, query)) return true;
    return std::any_of(aliases.begin(), aliases.end(), [&](const std::string& alias) {
        return containsCaseInsensitive(alias, query);
    });
}

ShipStoryDatabase ShipStoryDatabase::loadFromDirectory(const std::filesystem::path& directory) {
    const auto catalog = readCsv(directory / "ship_stories.csv");
    const auto c = columns(catalog, {"ship_id", "wiki_name", "canonical_name",
        "harmony_name", "aliases", "camp", "ship_type", "story_title", "image_file"});
    ShipStoryDatabase result;
    std::unordered_set<std::string> ids;
    for (const auto& row : catalog.rows) {
        ShipStory story{row[c.at("ship_id")], row[c.at("wiki_name")],
            row[c.at("canonical_name")], row[c.at("harmony_name")],
            split(row[c.at("aliases")]), row[c.at("camp")], row[c.at("ship_type")],
            row[c.at("story_title")], row[c.at("image_file")], {}};
        if (story.shipId.empty() || !ids.insert(story.shipId).second)
            throw std::runtime_error("duplicate/empty ship story id: " + story.shipId);
        if (story.canonicalName.empty())
            throw std::runtime_error("ship story canonical name is empty: " + story.shipId);
        if (story.harmonyName.find('|') != std::string::npos ||
            story.harmonyName.find('=') != std::string::npos)
            throw std::runtime_error("invalid harmony name: " + story.shipId);
        result.stories_.push_back(std::move(story));
    }
    const auto steps = readCsv(directory / "ship_story_steps.csv");
    const auto s = columns(steps, {"ship_id", "chapter_order", "chapter_title", "requirement", "reward"});
    for (const auto& row : steps.rows) {
        auto found = std::find_if(result.stories_.begin(), result.stories_.end(), [&](const ShipStory& story) {
            return story.shipId == row[s.at("ship_id")];
        });
        if (found == result.stories_.end()) throw std::runtime_error("story step references unknown ship id");
        found->steps.push_back({parseInt(row[s.at("chapter_order")], "chapter_order"),
            row[s.at("chapter_title")], row[s.at("requirement")], row[s.at("reward")]});
    }
    for (auto& story : result.stories_) {
        std::sort(story.steps.begin(), story.steps.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
        for (std::size_t i = 0; i < story.steps.size(); ++i)
            if (story.steps[i].order != static_cast<int>(i + 1))
                throw std::runtime_error(story.shipId + " chapter order is not continuous");
    }
    if (result.stories_.empty()) throw std::runtime_error("ship story catalog is empty");
    return result;
}

const std::vector<ShipStory>& ShipStoryDatabase::stories() const noexcept { return stories_; }

const ShipStory& ShipStoryDatabase::require(const std::string& shipId) const {
    const auto found = std::find_if(stories_.begin(), stories_.end(), [&](const auto& story) { return story.shipId == shipId; });
    if (found == stories_.end()) throw std::runtime_error("unknown story ship: " + shipId);
    return *found;
}

ShipStoryProgress::ShipStoryProgress(std::filesystem::path file, const ShipStoryDatabase& database,
                                     std::filesystem::path legacyShipsCsv)
    : file_(std::move(file)), database_(database), legacyShipsCsv_(std::move(legacyShipsCsv)) {}

void ShipStoryProgress::load() {
    entries_.clear();
    orphanEntries_.clear();
    if (!std::filesystem::exists(file_)) { seedFromLegacy(); save(); return; }
    const auto table = readCsv(file_);
    const auto c = columns(table, {"ship_id", "status", "is_max_level", "updated_at"});
    std::unordered_set<std::string> ids;
    for (const auto& row : table.rows) {
        const auto status = shipStoryStatusFromText(row[c.at("status")]);
        if (!status) throw std::runtime_error("unknown ship story status: " + row[c.at("status")]);
        bool maxLevel = parseBool(row[c.at("is_max_level")]);
        if (*status == ShipStoryStatus::Completed) maxLevel = true;
        if (!ids.insert(row[c.at("ship_id")]).second) throw std::runtime_error("duplicate ship progress id");
        ShipStoryProgressEntry entry{row[c.at("ship_id")], *status, maxLevel,
                                     row[c.at("updated_at")]};
        try { database_.require(entry.shipId); entries_.push_back(std::move(entry)); }
        catch (...) { orphanEntries_.push_back(std::move(entry)); }
    }
    const auto timestamp = nowIso();
    for (const auto& story : database_.stories())
        if (!ids.count(story.shipId))
            entries_.push_back({story.shipId, ShipStoryStatus::Unowned, false, timestamp});
}

void ShipStoryProgress::seedFromLegacy() {
    const auto legacy = legacyShips(legacyShipsCsv_);
    const auto timestamp = nowIso();
    for (const auto& story : database_.stories()) {
        bool maxLevel = false;
        const bool owned = isLegacyMatch(story, legacy, maxLevel);
        entries_.push_back({story.shipId, owned ? ShipStoryStatus::InProgress : ShipStoryStatus::Unowned,
                            owned && maxLevel, timestamp});
    }
}

void ShipStoryProgress::update(const std::string& shipId, ShipStoryStatus status,
                               std::optional<bool> isMaxLevel) {
    database_.require(shipId);
    auto& entry = const_cast<ShipStoryProgressEntry&>(require(shipId));
    entry.status = status;
    if (status == ShipStoryStatus::Completed) entry.isMaxLevel = true;
    else if (status == ShipStoryStatus::Unowned) entry.isMaxLevel = false;
    else if (isMaxLevel) entry.isMaxLevel = *isMaxLevel;
    entry.updatedAt = nowIso();
    save();
}

const std::vector<ShipStoryProgressEntry>& ShipStoryProgress::entries() const noexcept { return entries_; }

const ShipStoryProgressEntry& ShipStoryProgress::require(const std::string& shipId) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.shipId == shipId; });
    if (found == entries_.end()) throw std::runtime_error("unknown ship progress id: " + shipId);
    return *found;
}

void ShipStoryProgress::save() const {
    std::ostringstream output;
    output << "ship_id,status,is_max_level,updated_at\n";
    for (const auto& entry : entries_)
        output << csvEscape(entry.shipId) << ',' << toText(entry.status) << ','
               << (entry.isMaxLevel ? "true" : "false") << ',' << csvEscape(entry.updatedAt) << '\n';
    for (const auto& entry : orphanEntries_)
        output << csvEscape(entry.shipId) << ',' << toText(entry.status) << ','
               << (entry.isMaxLevel ? "true" : "false") << ',' << csvEscape(entry.updatedAt) << '\n';
    atomicWrite(file_, output.str());
}
