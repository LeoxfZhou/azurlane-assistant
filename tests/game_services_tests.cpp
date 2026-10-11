#include "game_services.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

#ifndef GAME_SHIP_STORY_DATA_DIR
#error "GAME_SHIP_STORY_DATA_DIR is required"
#endif
#ifndef GAME_SHIP_PROGRESS_SEED_FILE
#error "GAME_SHIP_PROGRESS_SEED_FILE is required"
#endif

namespace {

void expect(bool value, const std::string& message) {
    if (!value) throw std::runtime_error("test failed: " + message);
}

std::filesystem::path temporaryDirectory() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("game_gui_services_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void testReferenceData(const ShipStoryDatabase& stories) {
    expect(stories.stories().size() == 123, "ship story catalog must contain 123 ships");
    std::size_t steps = 0;
    for (const auto& story : stories.stories()) {
        expect(!story.canonicalName.empty(), "ship canonical name must not be empty");
        expect(story.harmonyName.find('|') == std::string::npos &&
               story.harmonyName.find('=') == std::string::npos,
               "ship harmony name must not contain a parsed WIKI field");
        expect(!story.camp.empty() && !story.shipType.empty(), "ship filters must be populated");
        expect(!story.steps.empty(), "each story must have chapters");
        steps += story.steps.size();
    }
    const auto u47 = std::find_if(stories.stories().begin(), stories.stories().end(),
        [](const auto& story) { return story.canonicalName == "U-47"; });
    expect(u47 != stories.stories().end(), "U-47 must exist in the story catalog");
    expect(u47->harmonyName == "优希娜" && u47->displayLabel() == "U-47（优希娜）",
           "dual ship names must render as canonical name followed by harmony name");
    expect(u47->matchesName("U-47") && u47->matchesName("优希娜"),
           "both canonical and harmony names must be searchable");
    const auto singleName = std::find_if(stories.stories().begin(), stories.stories().end(),
        [](const auto& story) { return story.harmonyName.empty(); });
    expect(singleName != stories.stories().end() &&
           singleName->displayLabel() == singleName->canonicalName,
           "single-name ships must not render empty parentheses");
    expect(steps == 862, "story catalog must contain 862 ordered chapters");
}

void testShipProgress(const ShipStoryDatabase& stories, const std::filesystem::path& temporary) {
    const auto file = temporary / "ship_story_progress.csv";
    const auto seed = temporary / "synthetic_seed.csv";
    { std::ofstream output(seed);
      output << "name,alternate_name,is_max_level\n"
             << stories.stories()[0].canonicalName << ",,true\n"
             << stories.stories()[1].canonicalName << ",,false\n"; }
    ShipStoryProgress progress(file, stories, seed);
    progress.load();
    expect(std::filesystem::exists(file), "first load must create the migrated progress file");
    const auto owned = std::count_if(progress.entries().begin(), progress.entries().end(), [](const auto& entry) {
        return entry.status == ShipStoryStatus::InProgress;
    });
    const auto max = std::count_if(progress.entries().begin(), progress.entries().end(), [](const auto& entry) {
        return entry.status == ShipStoryStatus::InProgress && entry.isMaxLevel;
    });
    expect(owned == 2, "synthetic migration must mark two ships in progress");
    expect(max == 1, "synthetic migration must preserve one max-level ship");
    ShipStoryProgress fresh(temporary / "fresh.csv", stories, temporary / "missing_seed.csv");
    fresh.load();
    expect(std::all_of(fresh.entries().begin(), fresh.entries().end(), [](const auto& entry) {
        return entry.status == ShipStoryStatus::Unowned && !entry.isMaxLevel;
    }), "public installation without a seed must start entirely unowned");

    const auto id = progress.entries().front().shipId;
    progress.update(id, ShipStoryStatus::Completed, false);
    expect(progress.require(id).isMaxLevel, "completed story must imply max level");
    progress.update(id, ShipStoryStatus::InProgress);
    expect(progress.require(id).isMaxLevel, "restoring unfinished state must preserve max level");
    progress.update(id, ShipStoryStatus::Unowned);
    expect(!progress.require(id).isMaxLevel, "unowned state must clear max level");

    ShipStoryProgress reloaded(file, stories, GAME_SHIP_PROGRESS_SEED_FILE);
    reloaded.load();
    expect(reloaded.require(id).status == ShipStoryStatus::Unowned,
           "progress mutation must persist atomically");
}

void testCorruptFilesArePreserved(const ShipStoryDatabase& stories,
                                  const std::filesystem::path& temporary) {
    const auto shipFile = temporary / "bad_ships.csv";
    { std::ofstream output(shipFile); output << "broken,data\n"; }
    bool failed = false;
    try { ShipStoryProgress progress(shipFile, stories, GAME_SHIP_PROGRESS_SEED_FILE); progress.load(); }
    catch (...) { failed = true; }
    expect(failed, "corrupt ship progress must fail loudly");
    std::ifstream shipInput(shipFile);
    std::string shipContent((std::istreambuf_iterator<char>(shipInput)), {});
    expect(shipContent == "broken,data\n", "corrupt ship progress must not be overwritten");

}

}  // namespace

int main() {
    const auto temporary = temporaryDirectory();
    try {
        const auto stories = ShipStoryDatabase::loadFromDirectory(GAME_SHIP_STORY_DATA_DIR);
        testReferenceData(stories);
        testShipProgress(stories, temporary);
        testCorruptFilesArePreserved(stories, temporary);
        std::filesystem::remove_all(temporary);
        std::cout << "game GUI service tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(temporary);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
