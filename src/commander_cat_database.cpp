#include "commander_cat_database.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace {

using CsvRows = std::vector<std::vector<std::string>>;

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& value,
                               const std::string& delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto found = value.find(delimiter, start);
        result.push_back(trim(value.substr(start, found - start)));
        if (found == std::string::npos) break;
        start = found + delimiter.size();
    }
    return result;
}

CsvRows readCsv(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw CommanderCatDataError("无法打开指挥猫数据文件：" + path.string());
    CsvRows rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    char character = 0;
    while (input.get(character)) {
        if (quoted) {
            if (character == '"') {
                if (input.peek() == '"') { input.get(character); field.push_back('"'); }
                else quoted = false;
            } else {
                field.push_back(character);
            }
        } else if (character == '"') {
            quoted = true;
        } else if (character == ',') {
            row.push_back(field); field.clear();
        } else if (character == '\n') {
            if (!field.empty() && field.back() == '\r') field.pop_back();
            row.push_back(field); field.clear(); rows.push_back(row); row.clear();
        } else {
            field.push_back(character);
        }
    }
    if (quoted) throw CommanderCatDataError(path.string() + "：CSV 引号没有闭合");
    if (!field.empty() || !row.empty()) { row.push_back(field); rows.push_back(row); }
    if (rows.empty()) throw CommanderCatDataError(path.string() + "：CSV 为空");
    return rows;
}

std::unordered_map<std::string, std::size_t> headerIndex(
    const std::filesystem::path& path,
    const CsvRows& rows,
    const std::vector<std::string>& required) {
    std::unordered_map<std::string, std::size_t> result;
    for (std::size_t i = 0; i < rows[0].size(); ++i) result[rows[0][i]] = i;
    for (const auto& name : required) {
        if (!result.count(name)) throw CommanderCatDataError(path.string() + "：缺少字段 " + name);
    }
    return result;
}

std::string cell(const std::vector<std::string>& row,
                 const std::unordered_map<std::string, std::size_t>& index,
                 const std::string& name,
                 const std::filesystem::path& path,
                 std::size_t line) {
    const auto column = index.at(name);
    if (column >= row.size()) {
        throw CommanderCatDataError(path.string() + ":" + std::to_string(line) +
                                    "：字段数量不足，缺少 " + name);
    }
    return trim(row[column]);
}

int parseInt(const std::string& value, const std::string& context) {
    try { std::size_t used = 0; int number = std::stoi(value, &used); if (used != value.size()) throw std::invalid_argument("x"); return number; }
    catch (...) { throw CommanderCatDataError(context + "：不是整数：" + value); }
}

double parseDouble(const std::string& value, const std::string& context) {
    try { std::size_t used = 0; double number = std::stod(value, &used); if (used != value.size()) throw std::invalid_argument("x"); return number; }
    catch (...) { throw CommanderCatDataError(context + "：不是数值：" + value); }
}

bool parseBool(const std::string& value, const std::string& context) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw CommanderCatDataError(context + "：应为 true 或 false");
}

TalentKind parseKind(const std::string& value) {
    if (value == "normal") return TalentKind::Normal;
    if (value == "special") return TalentKind::Special;
    if (value == "fixed") return TalentKind::Fixed;
    if (value == "placeholder") return TalentKind::Placeholder;
    throw CommanderCatDataError("未知天赋类型：" + value);
}

CatSlot parseSlot(const std::string& value) {
    if (value == "commander") return CatSlot::Commander;
    if (value == "staff") return CatSlot::Staff;
    if (value == "either") return CatSlot::Either;
    throw CommanderCatDataError("未知职位：" + value);
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsInsensitive(const std::string& haystack, const std::string& needle) {
    return lowerAscii(haystack).find(lowerAscii(needle)) != std::string::npos;
}

}  // namespace

CommanderCatDatabase CommanderCatDatabase::loadFromDirectory(
    const std::filesystem::path& directory) {
    CommanderCatDatabase database;

    const auto talentPath = directory / "commander_cat_talents.csv";
    const auto talentRows = readCsv(talentPath);
    const auto ti = headerIndex(talentPath, talentRows,
        {"talent_id","family_id","name","level","kind","effect","aliases"});
    for (std::size_t line = 2; line <= talentRows.size(); ++line) {
        const auto& row = talentRows[line - 1];
        TalentDefinition talent;
        talent.talentId = cell(row, ti, "talent_id", talentPath, line);
        talent.familyId = cell(row, ti, "family_id", talentPath, line);
        talent.name = cell(row, ti, "name", talentPath, line);
        talent.level = parseInt(cell(row, ti, "level", talentPath, line), talentPath.string());
        talent.kind = parseKind(cell(row, ti, "kind", talentPath, line));
        talent.effect = cell(row, ti, "effect", talentPath, line);
        const auto aliases = cell(row, ti, "aliases", talentPath, line);
        if (!aliases.empty()) talent.aliases = split(aliases, "|");
        if (talent.talentId.empty() || talent.familyId.empty() || talent.name.empty())
            throw CommanderCatDataError(talentPath.string() + ":" + std::to_string(line) + "：天赋关键字段为空");
        if (database.talentById_.count(talent.talentId))
            throw CommanderCatDataError("天赋 ID 重复：" + talent.talentId);
        const std::size_t index = database.talents_.size();
        database.talentById_[talent.talentId] = index;
        database.talentByName_[talent.name] = index;
        for (const auto& alias : talent.aliases) database.talentByName_[alias] = index;
        database.talents_.push_back(std::move(talent));
    }

    const auto catPath = directory / "commander_cats.csv";
    const auto catRows = readCsv(catPath);
    const auto ci = headerIndex(catPath, catRows,
        {"cat_id","name","english_name","faction","rarity","skill_lv1_bonus",
         "skill_lv2_bonus","skill_lv3_bonus","commander_cat_review","source_images"});
    for (std::size_t line = 2; line <= catRows.size(); ++line) {
        const auto& row = catRows[line - 1];
        CommanderCatSpecies cat;
        cat.catId = cell(row,ci,"cat_id",catPath,line); cat.name = cell(row,ci,"name",catPath,line);
        cat.englishName = cell(row,ci,"english_name",catPath,line); cat.faction = cell(row,ci,"faction",catPath,line);
        cat.rarity = cell(row,ci,"rarity",catPath,line); cat.skillLv1Bonus = cell(row,ci,"skill_lv1_bonus",catPath,line);
        cat.skillLv2Bonus = cell(row,ci,"skill_lv2_bonus",catPath,line); cat.skillLv3Bonus = cell(row,ci,"skill_lv3_bonus",catPath,line);
        cat.review = cell(row,ci,"commander_cat_review",catPath,line); cat.sourceImages = cell(row,ci,"source_images",catPath,line);
        if (cat.faction == "风帆") cat.faction = "飓风";
        if (cat.rarity != "SSR" && cat.rarity != "SR" && cat.rarity != "R")
            throw CommanderCatDataError("未知稀有度：" + cat.rarity);
        if (database.speciesById_.count(cat.catId)) throw CommanderCatDataError("猫种 ID 重复：" + cat.catId);
        database.speciesById_[cat.catId] = database.species_.size();
        database.species_.push_back(std::move(cat));
    }

    const auto buildPath = directory / "commander_cat_builds.csv";
    const auto buildRows = readCsv(buildPath);
    const auto bi = headerIndex(buildPath, buildRows,
        {"build_id","category","cat_id","variant_order","initial_talents","target_talents",
         "position","notes","evaluation","source_image","difficulty_floor"});
    std::set<std::string> buildIds;
    for (std::size_t line = 2; line <= buildRows.size(); ++line) {
        const auto& row = buildRows[line - 1]; BuildPlan plan;
        plan.buildId=cell(row,bi,"build_id",buildPath,line); plan.category=cell(row,bi,"category",buildPath,line);
        plan.catId=cell(row,bi,"cat_id",buildPath,line); plan.variantOrder=parseInt(cell(row,bi,"variant_order",buildPath,line),buildPath.string());
        plan.initialTalentsText=cell(row,bi,"initial_talents",buildPath,line); plan.targetTalentsText=cell(row,bi,"target_talents",buildPath,line);
        plan.position=cell(row,bi,"position",buildPath,line); plan.notes=cell(row,bi,"notes",buildPath,line);
        plan.evaluation=cell(row,bi,"evaluation",buildPath,line); plan.sourceImage=cell(row,bi,"source_image",buildPath,line);
        plan.difficultyFloor=cell(row,bi,"difficulty_floor",buildPath,line);
        const auto separator = plan.position.find_last_of(' ');
        plan.buildStyle = separator == std::string::npos ? "纯伤" : plan.position.substr(separator + 1);
        if (!database.speciesById_.count(plan.catId)) throw CommanderCatDataError("培养方案引用未知猫种：" + plan.catId);
        if (!buildIds.insert(plan.buildId).second) throw CommanderCatDataError("培养方案 ID 重复：" + plan.buildId);
        plan.targetTalentGroups = parseTalentExpression(plan.targetTalentsText, database,
            buildPath.string() + ":" + std::to_string(line));
        // 初始方案也必须能落到规范词典；结果只用于数据一致性校验。
        static_cast<void>(parseTalentExpression(plan.initialTalentsText, database,
            buildPath.string() + ":" + std::to_string(line)));
        database.builds_.push_back(std::move(plan));
    }

    const auto rankingPath = directory / "commander_cat_rankings.csv";
    const auto rankingRows = readCsv(rankingPath);
    const auto ri = headerIndex(rankingPath, rankingRows,
        {"category","rank","cat_id","display_label","faction","rarity","position","slot",
         "build_style","scenario","is_stable","target_talents","k1","k2","k_percent","source_image"});
    std::map<std::string,std::set<int>> categoryRanks;
    for (std::size_t line=2; line<=rankingRows.size(); ++line) {
        const auto& row=rankingRows[line-1]; RankingRecord rank;
        rank.category=cell(row,ri,"category",rankingPath,line); rank.rank=parseInt(cell(row,ri,"rank",rankingPath,line),rankingPath.string());
        rank.catId=cell(row,ri,"cat_id",rankingPath,line); rank.displayLabel=cell(row,ri,"display_label",rankingPath,line);
        rank.faction=cell(row,ri,"faction",rankingPath,line); if(rank.faction=="风帆")rank.faction="飓风";
        rank.rarity=cell(row,ri,"rarity",rankingPath,line); rank.position=cell(row,ri,"position",rankingPath,line);
        rank.slot=parseSlot(cell(row,ri,"slot",rankingPath,line)); rank.buildStyle=cell(row,ri,"build_style",rankingPath,line);
        rank.scenario=cell(row,ri,"scenario",rankingPath,line); rank.stable=parseBool(cell(row,ri,"is_stable",rankingPath,line),rankingPath.string());
        rank.targetTalentsText=cell(row,ri,"target_talents",rankingPath,line);
        rank.targetTalentGroups=parseTalentExpression(rank.targetTalentsText,database,rankingPath.string()+":"+std::to_string(line));
        rank.k1=parseDouble(cell(row,ri,"k1",rankingPath,line),rankingPath.string());
        rank.k2=parseDouble(cell(row,ri,"k2",rankingPath,line),rankingPath.string());
        rank.kPercent=parseDouble(cell(row,ri,"k_percent",rankingPath,line),rankingPath.string());
        rank.sourceImage=cell(row,ri,"source_image",rankingPath,line);
        if(!database.speciesById_.count(rank.catId))throw CommanderCatDataError("排行引用未知猫种："+rank.catId);
        if(!categoryRanks[rank.category].insert(rank.rank).second)throw CommanderCatDataError("排行名次重复："+rank.category+" "+std::to_string(rank.rank));
        database.rankings_.push_back(std::move(rank));
    }
    const std::map<std::string,int> expected={{"战列",28},{"潜艇",17},{"重巡",8},{"轻巡",15},{"驱逐",24},{"突击者",10},{"航母",20}};
    if(database.rankings_.size()!=122)throw CommanderCatDataError("排行总数应为 122");
    for(const auto& [category,count]:expected){const auto& values=categoryRanks[category];if(values.size()!=static_cast<std::size_t>(count)||*values.begin()!=1||*values.rbegin()!=count)throw CommanderCatDataError(category+"排行必须从 1 连续到 "+std::to_string(count));}

    const auto poolPath=directory/"commander_cat_initial_pools.csv";
    const auto poolRows=readCsv(poolPath); const auto pi=headerIndex(poolPath,poolRows,{"cat_id","talent_id","source_note"});
    for(std::size_t line=2;line<=poolRows.size();++line){const auto cat=cell(poolRows[line-1],pi,"cat_id",poolPath,line);const auto talent=cell(poolRows[line-1],pi,"talent_id",poolPath,line);database.requireSpecies(cat);database.requireTalent(talent);database.initialPools_[cat].insert(talent);}

    const auto fixedPath=directory/"commander_cat_fixed_routes.csv";
    const auto fixedRows=readCsv(fixedPath); const auto fi=headerIndex(fixedPath,fixedRows,{"cat_id","level","talent_ids","description"});
    for(std::size_t line=2;line<=fixedRows.size();++line){const auto& row=fixedRows[line-1];FixedRouteNode node;node.catId=cell(row,fi,"cat_id",fixedPath,line);node.level=parseInt(cell(row,fi,"level",fixedPath,line),fixedPath.string());node.description=cell(row,fi,"description",fixedPath,line);for(const auto& id:split(cell(row,fi,"talent_ids",fixedPath,line)," + ")){database.requireTalent(id);node.talentIds.push_back(id);}database.requireSpecies(node.catId);database.fixedRoutes_.push_back(std::move(node));}
    for(auto& cat:database.species_)cat.fixedRoute=std::any_of(database.fixedRoutes_.begin(),database.fixedRoutes_.end(),[&cat](const FixedRouteNode& n){return n.catId==cat.catId;});

    const auto fixedRecommendationPath=directory/"commander_cat_fixed_recommendations.csv";
    const auto fixedRecommendationRows=readCsv(fixedRecommendationPath);const auto fri=headerIndex(fixedRecommendationPath,fixedRecommendationRows,{"recommendation_id","category","cat_id","slot","scenario","reason"});std::set<std::string> fixedRecommendationIds;const std::set<std::string> validCategories={"战列","航母","潜艇","重巡","轻巡","驱逐","突击者"};
    for(std::size_t line=2;line<=fixedRecommendationRows.size();++line){const auto& row=fixedRecommendationRows[line-1];FixedScenarioRecommendation recommendation;recommendation.recommendationId=cell(row,fri,"recommendation_id",fixedRecommendationPath,line);recommendation.category=cell(row,fri,"category",fixedRecommendationPath,line);recommendation.catId=cell(row,fri,"cat_id",fixedRecommendationPath,line);recommendation.slot=parseSlot(cell(row,fri,"slot",fixedRecommendationPath,line));recommendation.scenario=cell(row,fri,"scenario",fixedRecommendationPath,line);recommendation.reason=cell(row,fri,"reason",fixedRecommendationPath,line);if(!fixedRecommendationIds.insert(recommendation.recommendationId).second)throw CommanderCatDataError("固定猫场景推荐 ID 重复："+recommendation.recommendationId);if(!validCategories.count(recommendation.category))throw CommanderCatDataError("固定猫场景推荐类别无效："+recommendation.category);const auto& cat=database.requireSpecies(recommendation.catId);if(!cat.fixedRoute)throw CommanderCatDataError("固定猫场景推荐引用了非固定成长猫："+recommendation.catId);if(recommendation.scenario.empty()||recommendation.reason.empty())throw CommanderCatDataError("固定猫场景推荐条件或理由为空："+recommendation.recommendationId);const auto route=database.fixedRouteFor(recommendation.catId);if(route.empty()||route.back()->level!=30)throw CommanderCatDataError("固定猫场景推荐缺少30级路线："+recommendation.catId);database.fixedRecommendations_.push_back(std::move(recommendation));}

    const auto guidePath=directory/"commander_cat_guide_recommendations.csv";
    const auto guideRows=readCsv(guidePath);
    const auto gi=headerIndex(guidePath,guideRows,{"guide_id","cat_id","scenario","slot","initial_specials","initial_normals","required_talents","optional_talents","recommendation","notes","covered"});
    std::set<std::string> guideIds;
    for(std::size_t line=2;line<=guideRows.size();++line){const auto& row=guideRows[line-1];GuideRecommendation guide;guide.guideId=cell(row,gi,"guide_id",guidePath,line);guide.catId=cell(row,gi,"cat_id",guidePath,line);guide.scenario=cell(row,gi,"scenario",guidePath,line);guide.slot=parseSlot(cell(row,gi,"slot",guidePath,line));guide.initialSpecials=cell(row,gi,"initial_specials",guidePath,line);guide.initialNormals=cell(row,gi,"initial_normals",guidePath,line);guide.requiredTalents=cell(row,gi,"required_talents",guidePath,line);guide.optionalTalents=cell(row,gi,"optional_talents",guidePath,line);guide.recommendation=cell(row,gi,"recommendation",guidePath,line);guide.notes=cell(row,gi,"notes",guidePath,line);guide.covered=parseBool(cell(row,gi,"covered",guidePath,line),guidePath.string());if(!guideIds.insert(guide.guideId).second)throw CommanderCatDataError("攻略记录 ID 重复："+guide.guideId);database.requireSpecies(guide.catId);for(const auto* expression:{&guide.initialSpecials,&guide.initialNormals,&guide.requiredTalents,&guide.optionalTalents})if(!expression->empty())parseTalentExpression(*expression,database,guidePath.string()+":"+std::to_string(line));database.guideRecommendations_.push_back(std::move(guide));}

    const auto blockPath=directory/"commander_cat_nickname_blocklist.txt";
    std::ifstream blockInput(blockPath);if(!blockInput)throw CommanderCatDataError("无法打开昵称屏蔽片段文件："+blockPath.string());
    std::string blocked;while(std::getline(blockInput,blocked)){if(!blocked.empty()&&blocked.back()=='\r')blocked.pop_back();blocked=trim(blocked);if(!blocked.empty()&&blocked.front()!='#')database.nicknameBlockedTokens_.push_back(blocked);}
    if(database.species_.size()!=34)throw CommanderCatDataError("指挥猫种类应为 34，只读到 "+std::to_string(database.species_.size()));
    return database;
}

const std::vector<CommanderCatSpecies>& CommanderCatDatabase::species() const noexcept{return species_;}
const std::vector<TalentDefinition>& CommanderCatDatabase::talents() const noexcept{return talents_;}
const std::vector<BuildPlan>& CommanderCatDatabase::builds() const noexcept{return builds_;}
const std::vector<RankingRecord>& CommanderCatDatabase::rankings() const noexcept{return rankings_;}
const std::vector<FixedRouteNode>& CommanderCatDatabase::fixedRoutes() const noexcept{return fixedRoutes_;}
const std::vector<FixedScenarioRecommendation>& CommanderCatDatabase::fixedRecommendations() const noexcept{return fixedRecommendations_;}
const std::vector<GuideRecommendation>& CommanderCatDatabase::guideRecommendations() const noexcept{return guideRecommendations_;}

const CommanderCatSpecies& CommanderCatDatabase::requireSpecies(const std::string& id) const{const auto it=speciesById_.find(id);if(it==speciesById_.end())throw CommanderCatDataError("未知猫种 ID："+id);return species_[it->second];}
const TalentDefinition& CommanderCatDatabase::requireTalent(const std::string& id) const{const auto it=talentById_.find(id);if(it==talentById_.end())throw CommanderCatDataError("未知天赋 ID："+id);return talents_[it->second];}
const TalentDefinition* CommanderCatDatabase::findTalentByName(const std::string& name) const{const auto it=talentByName_.find(trim(name));return it==talentByName_.end()?nullptr:&talents_[it->second];}

std::vector<const CommanderCatSpecies*> CommanderCatDatabase::findSpecies(const std::string& text) const{
    auto query=trim(text);if(query=="风帆")query="飓风";std::vector<const CommanderCatSpecies*> exact,partial;
    for(const auto& cat:species_){if(cat.catId==query||cat.name==query||lowerAscii(cat.englishName)==lowerAscii(query)||cat.faction==query)exact.push_back(&cat);else if(containsInsensitive(cat.catId,query)||containsInsensitive(cat.name,query)||containsInsensitive(cat.englishName,query)||containsInsensitive(cat.faction,query))partial.push_back(&cat);}
    return exact.empty()?partial:exact;
}

std::vector<const TalentDefinition*> CommanderCatDatabase::findTalentFamilies(const std::string& text) const{
    const auto query=trim(text);std::vector<const TalentDefinition*> exact,partial;std::set<std::string> seen;
    for(const auto& talent:talents_){if(talent.kind!=TalentKind::Normal||talent.level!=1)continue;bool e=talent.name==query, p=containsInsensitive(talent.name,query);for(const auto& a:talent.aliases){e=e||a==query;p=p||containsInsensitive(a,query);}if(e&&seen.insert(talent.familyId).second)exact.push_back(&talent);else if(p&&seen.insert(talent.familyId).second)partial.push_back(&talent);}
    return exact.empty()?partial:exact;
}

std::vector<const BuildPlan*> CommanderCatDatabase::buildsFor(const std::string& id) const{std::vector<const BuildPlan*> result;for(const auto& x:builds_)if(x.catId==id)result.push_back(&x);return result;}
std::vector<const RankingRecord*> CommanderCatDatabase::rankingsFor(const std::string& id,const std::string& category) const{std::vector<const RankingRecord*> result;for(const auto& x:rankings_)if(x.catId==id&&x.category==category)result.push_back(&x);return result;}
std::vector<const FixedRouteNode*> CommanderCatDatabase::fixedRouteFor(const std::string& id) const{std::vector<const FixedRouteNode*> result;for(const auto& x:fixedRoutes_)if(x.catId==id)result.push_back(&x);std::sort(result.begin(),result.end(),[](auto a,auto b){return a->level<b->level;});return result;}
std::vector<const FixedScenarioRecommendation*> CommanderCatDatabase::fixedRecommendationsFor(const std::string& category) const{std::vector<const FixedScenarioRecommendation*> result;for(const auto& x:fixedRecommendations_)if(x.category==category)result.push_back(&x);return result;}
std::vector<const GuideRecommendation*> CommanderCatDatabase::guideRecommendationsFor(const std::string& catId) const{std::vector<const GuideRecommendation*> result;for(const auto& x:guideRecommendations_)if(x.catId==catId)result.push_back(&x);return result;}
bool CommanderCatDatabase::nicknameAllowed(const std::string& nickname) const{for(const auto& token:nicknameBlockedTokens_)if(nickname.find(token)!=std::string::npos)return false;return true;}
const std::unordered_set<std::string>& CommanderCatDatabase::initialPoolFor(const std::string& id) const{static const std::unordered_set<std::string> empty;const auto it=initialPools_.find(id);return it==initialPools_.end()?empty:it->second;}

std::vector<TalentChoiceGroup> parseTalentExpression(const std::string& expression,const CommanderCatDatabase& database,const std::string& context){
    std::vector<TalentChoiceGroup> groups;if(trim(expression).empty())return groups;
    for(const auto& part:split(expression," + ")){TalentChoiceGroup group;for(const auto& option:split(part," 或 ")){const auto* talent=database.findTalentByName(option);if(!talent)throw CommanderCatDataError(context+"：天赋未在规范词典中："+option);group.push_back(talent->talentId);}if(!group.empty())groups.push_back(std::move(group));}
    return groups;
}
