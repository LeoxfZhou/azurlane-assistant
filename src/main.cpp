#include "commander_cat_assessment.hpp"
#include "commander_cat_embryo.hpp"
#include "commander_cat_fleets.hpp"
#include "commander_cat_database.hpp"
#include "commander_cat_inventory.hpp"
#include "commander_cat_settings.hpp"
#include "game_services.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>

#ifndef AZUR_LANE_COMMANDER_CAT_DATA_DIR
#error "GUI target needs AZUR_LANE_COMMANDER_CAT_DATA_DIR"
#endif
#ifndef GAME_USER_DATA_DIR
#error "GUI target needs GAME_USER_DATA_DIR"
#endif
#ifndef GAME_GUI_WEB_DIR
#error "GUI target needs GAME_GUI_WEB_DIR"
#endif
#ifndef GAME_DATA_DIR
#error "GUI target needs GAME_DATA_DIR"
#endif
#ifndef GAME_CACHE_DIR
#error "GUI target needs GAME_CACHE_DIR"
#endif
#ifndef AZUR_LANE_WIKI_REFRESH_SCRIPT
#error "GUI target needs AZUR_LANE_WIKI_REFRESH_SCRIPT"
#endif
#ifndef AZUR_LANE_WIKI_REFRESH_PREVIEW
#error "GUI target needs AZUR_LANE_WIKI_REFRESH_PREVIEW"
#endif
#ifndef AZUR_LANE_PYTHON_EXECUTABLE
#error "GUI target needs AZUR_LANE_PYTHON_EXECUTABLE"
#endif
#ifndef GAME_SHIP_PROGRESS_SEED_FILE
#error "GUI target needs GAME_SHIP_PROGRESS_SEED_FILE"
#endif
#ifndef GAME_SHIP_STORY_DATA_DIR
#error "GUI target needs GAME_SHIP_STORY_DATA_DIR"
#endif
#ifndef GAME_DATA_REFRESH_SCRIPT
#error "GUI target needs GAME_DATA_REFRESH_SCRIPT"
#endif
#ifndef COMMANDER_CAT_IMPORT_SCRIPT
#error "GUI target needs COMMANDER_CAT_IMPORT_SCRIPT"
#endif
#ifndef GAME_GUI_DEFAULT_ROUTE
#define GAME_GUI_DEFAULT_ROUTE "home"
#endif

namespace {

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20)
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
                else out << c;
        }
    }
    return out.str();
}

std::string json(const std::string& value) { return "\"" + escapeJson(value) + "\""; }

std::string readFile(const std::filesystem::path& path, bool binary = false) {
    std::ifstream input(path, binary ? std::ios::binary : std::ios::in);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), {}};
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("无法写入暂存文件：" + path.string());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) throw std::runtime_error("写入暂存文件失败：" + path.string());
}

bool validUtf8(const std::string& value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto byte = static_cast<unsigned char>(value[i]);
        std::size_t continuation = 0;
        if (byte <= 0x7f) { ++i; continue; }
        if (byte >= 0xc2 && byte <= 0xdf) continuation = 1;
        else if (byte >= 0xe0 && byte <= 0xef) continuation = 2;
        else if (byte >= 0xf0 && byte <= 0xf4) continuation = 3;
        else return false;
        if (i + continuation >= value.size()) return false;
        for (std::size_t offset = 1; offset <= continuation; ++offset)
            if ((static_cast<unsigned char>(value[i + offset]) & 0xc0) != 0x80) return false;
        if (continuation == 2) {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((byte == 0xe0 && second < 0xa0) || (byte == 0xed && second >= 0xa0)) return false;
        }
        if (continuation == 3) {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((byte == 0xf0 && second < 0x90) || (byte == 0xf4 && second >= 0x90)) return false;
        }
        i += continuation + 1;
    }
    return true;
}

std::string decodeBase64(const std::string& value) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int bits = 0, count = 0;
    bool padding = false;
    for (unsigned char c : value) {
        if (c == '=') { padding = true; continue; }
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        if (padding) throw std::runtime_error("上传内容的 Base64 格式错误");
        const auto position = alphabet.find(static_cast<char>(c));
        if (position == std::string::npos)
            throw std::runtime_error("上传内容的 Base64 格式错误");
        bits = (bits << 6) | static_cast<int>(position);
        count += 6;
        if (count >= 8) {
            count -= 8;
            result.push_back(static_cast<char>((bits >> count) & 0xff));
        }
    }
    return result;
}

std::string timestampForPath() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

std::string urlDecode(const std::string& value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') result.push_back(' ');
        else if (value[i] == '%' && i + 2 < value.size()) {
            result.push_back(static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else result.push_back(value[i]);
    }
    return result;
}

std::map<std::string, std::string> form(const std::string& body) {
    std::map<std::string, std::string> result;
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end - start);
        const auto equal = part.find('=');
        result[urlDecode(part.substr(0, equal))] = equal == std::string::npos
            ? "" : urlDecode(part.substr(equal + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) if (!item.empty()) result.push_back(item);
    return result;
}

std::string names(const CommanderCatDatabase& database,
                  const std::vector<std::string>& ids) {
    std::string result;
    for (const auto& id : ids) {
        if (!result.empty()) result += " + ";
        result += database.requireTalent(id).name;
    }
    return result.empty() ? "无" : result;
}

std::string upgradeNames(const CommanderCatDatabase& database,
                         const std::vector<TalentUpgradeAction>& actions) {
    std::string result;
    for (const auto& action : actions) {
        if (!result.empty()) result += "；";
        for (std::size_t i = 0; i < action.pathTalentIds.size(); ++i) {
            if (i) result += " → ";
            result += database.requireTalent(action.pathTalentIds[i]).name;
        }
        if (action.requiredPoints > 0)
            result += "（" + std::to_string(action.requiredPoints) + "点）";
    }
    return result.empty() ? "无" : result;
}

std::string shellQuote(const std::string& value) {
    std::string result = "'";
    for (char c : value) {
        if (c == '\'') result += "'\\''";
        else result.push_back(c);
    }
    return result + "'";
}

struct Request {
    std::string method, path, body;
    std::map<std::string, std::string> headers;
};

Request parseRequest(int client) {
    std::string raw;
    char buffer[8192];
    std::size_t headerEnd = std::string::npos, contentLength = 0;
    while (true) {
        const auto count = recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0) break;
        raw.append(buffer, static_cast<std::size_t>(count));
        if (headerEnd == std::string::npos &&
            (headerEnd = raw.find("\r\n\r\n")) != std::string::npos) {
            const auto marker = raw.find("Content-Length:");
            if (marker != std::string::npos)
                contentLength = static_cast<std::size_t>(std::stoul(raw.substr(marker + 15)));
        }
        if (headerEnd != std::string::npos && raw.size() >= headerEnd + 4 + contentLength) break;
    }
    Request request;
    const auto first = raw.find("\r\n");
    std::istringstream line(raw.substr(0, first));
    line >> request.method >> request.path;
    std::size_t position = first + 2;
    while (position < headerEnd) {
        const auto end = raw.find("\r\n", position);
        const auto text = raw.substr(position, end - position);
        const auto colon = text.find(':');
        if (colon != std::string::npos) {
            auto key = text.substr(0, colon), value = text.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            request.headers[key] = value;
        }
        position = end + 2;
    }
    if (headerEnd != std::string::npos)
        request.body = raw.substr(headerEnd + 4, contentLength);
    return request;
}

void respond(int client, int status, const std::string& type, const std::string& body) {
    const std::string reason = status == 200 ? "OK" : status == 400 ? "Bad Request" :
        status == 403 ? "Forbidden" : status == 404 ? "Not Found" : "Internal Server Error";
    std::ostringstream header;
    header << "HTTP/1.1 " << status << ' ' << reason << "\r\nContent-Type: " << type
           << "\r\nContent-Length: " << body.size()
           << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    const auto value = header.str();
    send(client, value.data(), value.size(), 0);
    send(client, body.data(), body.size(), 0);
}

std::filesystem::path userDataDirectory() {
    const char* value = std::getenv("GAME_GUI_USER_DATA_DIR");
    if (!value || !*value) value = std::getenv("AZUR_LANE_GUI_USER_DATA_DIR");
    return std::filesystem::path(value && *value ? value : GAME_USER_DATA_DIR);
}

std::filesystem::path cacheDirectory() {
    const char* value = std::getenv("GAME_GUI_CACHE_DIR");
    return std::filesystem::path(value && *value ? value : GAME_CACHE_DIR);
}

class GuiApp {
public:
    GuiApp()
        : database_(CommanderCatDatabase::loadFromDirectory(AZUR_LANE_COMMANDER_CAT_DATA_DIR)),
          inventory_(userDataDirectory() / "commander_cat_inventory.csv"),
          settings_(userDataDirectory() / "commander_cat_settings.csv"),
          assessor_(database_), pairer_(database_, assessor_),
          shipDatabase_(ShipStoryDatabase::loadFromDirectory(GAME_SHIP_STORY_DATA_DIR)),
          shipProgress_(userDataDirectory() / "ship_story_progress.csv", shipDatabase_,
                        GAME_SHIP_PROGRESS_SEED_FILE) {
        std::filesystem::create_directories(userDataDirectory());
        std::filesystem::create_directories(cacheDirectory());
        inventory_.load();
        settings_.load();
        shipProgress_.load();
        std::random_device random;
        std::ostringstream token;
        for (int i = 0; i < 4; ++i) token << std::hex << random();
        token_ = token.str();
    }

    void run() {
        std::signal(SIGPIPE, SIG_IGN);
        int server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0) throw std::runtime_error("cannot create socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throw std::runtime_error("cannot bind local server");
        if (listen(server, 16) < 0) throw std::runtime_error("cannot listen");
        socklen_t length = sizeof(address);
        getsockname(server, reinterpret_cast<sockaddr*>(&address), &length);
        const std::string baseUrl = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        const std::string url = baseUrl + "/#/" + GAME_GUI_DEFAULT_ROUTE;
        std::cout << "碧蓝航线助手 已启动：" << url << "\n按 Ctrl+C 停止。\n" << std::flush;
        if (std::getenv("GAME_GUI_NO_BROWSER") == nullptr &&
            std::getenv("AZUR_LANE_GUI_NO_BROWSER") == nullptr)
            std::system(("open '" + url + "' >/dev/null 2>&1").c_str());
        while (true) {
            const int client = accept(server, nullptr, nullptr);
            if (client < 0) continue;
            try { handle(client); }
            catch (const std::exception& error) {
                respond(client, 500, "application/json; charset=utf-8",
                        "{\"error\":" + json(error.what()) + "}");
            }
            close(client);
        }
    }

private:
    std::pair<int, std::string> equipmentCommand(const std::string& action,
                                                const std::string& input = "{}") const {
        if (input.size() > 4 * 1024 * 1024) throw std::runtime_error("装备库存文件超过 4 MB");
        const auto directory = cacheDirectory() / ("equipment-session-" + token_);
        std::filesystem::create_directories(directory);
        writeFile(directory / "request.json", input);
        const auto script = std::filesystem::path(GAME_DATA_REFRESH_SCRIPT).parent_path() / "equipment_store.py";
        const std::string command = shellQuote(AZUR_LANE_PYTHON_EXECUTABLE) + " " + shellQuote(script.string()) +
            " " + shellQuote(action) + " --catalog " +
            shellQuote((std::filesystem::path(GAME_DATA_DIR) / "equipment/catalog.json").string()) +
            " --user-file " + shellQuote((userDataDirectory() / "equipment_inventory.json").string()) +
            " --input " + shellQuote((directory / "request.json").string()) +
            " >" + shellQuote((directory / "result.json").string()) + " 2>&1";
        const int status = std::system(command.c_str());
        auto result = readFile(directory / "result.json");
        if (result.empty()) result = "{\"error\":\"装备服务没有返回数据\"}";
        return {status, result};
    }

    std::pair<int,std::string> fleetCommand(const std::string& action, const std::string& input="{}") const {
        if(input.size()>1024*1024)throw std::runtime_error("编队数据过大");
        const auto directory=cacheDirectory()/("fleet-session-"+token_);
        std::filesystem::create_directories(directory);
        writeFile(directory/"request.json",input);
        const auto script=std::filesystem::path(GAME_DATA_REFRESH_SCRIPT).parent_path()/"fleet_plans.py";
        const std::string command=shellQuote(AZUR_LANE_PYTHON_EXECUTABLE)+" "+shellQuote(script.string())+
            " "+action+" --catalog "+shellQuote((std::filesystem::path(GAME_DATA_DIR)/"fleet_ships").string())+
            " --user-file "+shellQuote((userDataDirectory()/"fleet_plans.json").string())+
            " --input "+shellQuote((directory/"request.json").string())+
            " --team-output "+shellQuote((directory/"teams.tsv").string())+
            " --computed-result "+shellQuote((directory/"computed.json").string())+
            " >"+shellQuote((directory/"result.json").string())+" 2>&1";
        const int status=std::system(command.c_str());
        auto result=readFile(directory/"result.json");
        if(result.empty())result="{\"error\":\"编队服务没有返回数据\"}";
        return {status,result};
    }

    std::string fleetMemberJson(const FleetCatChoice& member) const {
        const auto embryo=assessEmbryo(database_,member.cat);
        const auto& species=database_.requireSpecies(member.cat.catId);
        std::ostringstream out;
        out<<"{\"id\":"<<json(member.cat.instanceId)<<",\"catId\":"<<json(member.cat.catId)
           <<",\"name\":"<<json(species.name)<<",\"nickname\":"<<json(member.cat.nickname)
           <<",\"trainingPlan\":"<<trainingPlanJson(member.route)
           <<",\"grade\":"<<json(embryo.tier)<<",\"talentValue\":"<<member.talentValue
           <<",\"skillValue\":"<<member.skillValue<<",\"target\":"<<json(names(database_,member.route.targetTalentIds))
           <<",\"retained\":"<<json(names(database_,member.route.retainedTalentIds))
           <<",\"acquire\":"<<json(names(database_,member.route.acquireTalentIds))
           <<",\"upgrades\":"<<json(upgradeNames(database_,member.route.upgradeActions))
           <<",\"replacement\":"<<json(names(database_,member.route.replacementTalentIds))
           <<",\"flexible\":"<<json(names(database_,member.route.flexibleInitialTalentIds))
           <<",\"points\":"<<member.route.requiredPoints<<",\"reasons\":[";
        for(std::size_t i=0;i<member.reasons.size();++i){if(i)out<<',';out<<json(member.reasons[i]);}
        out<<"],\"targetTalentIds\":[";
        for(std::size_t i=0;i<member.route.targetTalentIds.size();++i){if(i)out<<',';out<<json(member.route.targetTalentIds[i]);}
        out<<"]}";return out.str();
    }

    std::string realFleetRecommendations(bool theoretical=false) const {
        std::istringstream input(readFile(cacheDirectory()/("fleet-session-"+token_)/"teams.tsv"));
        std::vector<FleetTeam> teams;std::string line;
        while(std::getline(input,line)){
            const auto fields=split(line,'\t');
            if(fields.size()!=6&&fields.size()!=7&&fields.size()!=8)throw std::runtime_error("编队输入格式错误");
            if(fields[0]=="TEAM"){teams.push_back({fields[1],fields[2],fields[3],fields[4],fields[5],{}});teams.back().priority=fields.size()>=7?std::stoi(fields[6]):1;teams.back().planId=fields.size()==8?fields[7]:"";}
            else if(fields[0]=="SHIP"&&!teams.empty())teams.back().ships.push_back({fields[1],fields[2],fields[3],fields[4],std::stoi(fields[5])});
            else throw std::runtime_error("未知编队记录");
        }
        std::vector<InventoryCat> hypothetical;
        if(theoretical)for(const auto& species:database_.species()){
            InventoryCat cat;cat.catId=species.catId;cat.instanceId="theory_"+species.catId;
            hypothetical.push_back(cat);
        }
        const auto recommendations=recommendRealFleets(database_,assessor_,theoretical?hypothetical:inventory_.cats(),teams,theoretical);
        std::ostringstream out;out<<"{\"ok\":true,\"teams\":[";
        for(std::size_t i=0;i<recommendations.size();++i){
            if(i)out<<',';const auto& r=recommendations[i];
            out<<"{\"planId\":"<<json(r.team.planId)<<",\"id\":"<<json(r.team.id)<<",\"name\":"<<json(r.team.name)<<",\"warning\":"<<json(r.warning)
               <<",\"complete\":"<<(r.complete?"true":"false");
            if(r.complete)out<<",\"commander\":"<<fleetMemberJson(r.commander)<<",\"staff\":"<<fleetMemberJson(r.staff);
            out<<'}';
        }
        out<<"]}";return out.str();
    }

    std::string embryoUsesJson(const InventoryCat& cat) const {
        const auto assessment = assessEmbryo(database_, cat);
        std::ostringstream out;
        out << '[';
        bool first = true;
        for (const auto& use : assessment.uses) {
            if (!first) out << ',';
            first = false;
            out << "{\"name\":" << json(use.name) << ",\"tier\":" << json(use.tier)
                << ",\"specialValue\":" << use.specialValue
                << ",\"normalPoints\":" << use.normalPoints << ",\"contributions\":[";
            bool firstTalent = true;
            for (const auto& contribution : use.contributions) {
                if (!firstTalent) out << ',';
                firstTalent = false;
                out << "{\"name\":" << json(database_.requireTalent(contribution.talentId).name)
                    << ",\"specialValue\":" << contribution.specialValue
                    << ",\"normalPoints\":" << contribution.normalPoints << '}';
            }
            out << "]}";
        }
        out << ']';
        return out.str();
    }

    struct PendingCatImport {
        std::string id;
        std::optional<std::string> inventory;
        std::optional<std::string> settings;
        std::size_t oldInventoryCount = 0;
        std::size_t newInventoryCount = 0;
        std::vector<std::string> added;
        std::vector<std::string> removed;
        std::vector<std::string> changed;
        std::vector<std::string> settingChanges;
    };

    static bool sameInventoryCat(const InventoryCat& left, const InventoryCat& right) {
        return left.catId == right.catId && left.nickname == right.nickname &&
               left.initialTalentIds == right.initialTalentIds;
    }

    void validateImportedCat(const InventoryCat& cat) const {
        const auto& species = database_.requireSpecies(cat.catId);
        std::set<std::string> families;
        for (const auto& talentId : cat.initialTalentIds) {
            const auto& talent = database_.requireTalent(talentId);
            if (!families.insert(talent.familyId).second)
                throw std::runtime_error(cat.instanceId + " 存在重复天赋成长链：" + talent.familyId);
        }
        if (species.fixedRoute) {
            const auto route = database_.fixedRouteFor(species.catId);
            if (route.empty()) throw std::runtime_error(species.name + " 缺少固定初始路线");
            auto expected = route.front()->talentIds;
            auto actual = cat.initialTalentIds;
            std::sort(expected.begin(), expected.end());
            std::sort(actual.begin(), actual.end());
            if (actual != expected)
                throw std::runtime_error(cat.instanceId + " 的固定初始天赋与图鉴不一致");
        } else {
            validateInitialTalents(species, cat.initialTalentIds, true);
        }
    }

    std::string pendingImportJson(const PendingCatImport& pending) const {
        auto arrayJson = [](const std::vector<std::string>& values) {
            std::ostringstream out;
            out << '[';
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i) out << ',';
                out << json(values[i]);
            }
            return out.str() + ']';
        };
        std::ostringstream out;
        out << "{\"ok\":true,\"importId\":" << json(pending.id)
            << ",\"includesInventory\":" << (pending.inventory ? "true" : "false")
            << ",\"includesSettings\":" << (pending.settings ? "true" : "false")
            << ",\"oldInventoryCount\":" << pending.oldInventoryCount
            << ",\"newInventoryCount\":" << pending.newInventoryCount
            << ",\"added\":" << arrayJson(pending.added)
            << ",\"removed\":" << arrayJson(pending.removed)
            << ",\"changed\":" << arrayJson(pending.changed)
            << ",\"settingChanges\":" << arrayJson(pending.settingChanges) << '}';
        return out.str();
    }

    std::string prepareCatImport(const std::map<std::string, std::string>& values) {
        pendingImport_.reset();
        int fileCount = 0;
        try {
            std::size_t used = 0;
            fileCount = std::stoi(values.at("file_count"), &used);
            if (used != values.at("file_count").size()) throw std::invalid_argument("trailing");
        } catch (...) { throw std::runtime_error("导入文件数量无效"); }
        if (fileCount < 1 || fileCount > 2)
            throw std::runtime_error("一次请选择一个或两个文件");

        const auto root = cacheDirectory() / "commander_cat_windows_import";
        const auto uploads = root / "uploads";
        const auto extracted = root / "extracted";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(uploads, error);
        if (error) throw std::runtime_error("无法创建导入暂存目录：" + error.message());

        std::vector<std::filesystem::path> inputPaths;
        for (int index = 0; index < fileCount; ++index) {
            const auto suffix = std::to_string(index);
            const auto nameFound = values.find("file" + suffix + "_name");
            const auto dataFound = values.find("file" + suffix + "_data");
            if (nameFound == values.end() || dataFound == values.end())
                throw std::runtime_error("上传文件信息不完整");
            const auto& originalName = nameFound->second;
            if (originalName.find('/') != std::string::npos ||
                originalName.find('\\') != std::string::npos || originalName.empty())
                throw std::runtime_error("上传文件名不安全");
            if (dataFound->second.size() > 6 * 1024 * 1024)
                throw std::runtime_error("上传文件超过大小限制");
            const bool zip = originalName.size() >= 4 &&
                originalName.substr(originalName.size() - 4) == ".zip";
            const auto path = uploads / (zip ? "upload-" + suffix + ".zip" : originalName);
            writeFile(path, decodeBase64(dataFound->second));
            inputPaths.push_back(path);
        }

        const auto resultPath = root / "prepare-result.json";
        std::string command = shellQuote(AZUR_LANE_PYTHON_EXECUTABLE) + " " +
            shellQuote(COMMANDER_CAT_IMPORT_SCRIPT) + " --output " +
            shellQuote(extracted.string());
        for (const auto& path : inputPaths) command += " " + shellQuote(path.string());
        command += " >" + shellQuote(resultPath.string()) + " 2>&1";
        const int status = std::system(command.c_str());
        const auto preparation = readFile(resultPath);
        if (status != 0)
            throw std::runtime_error("导入文件未通过安全检查：" + preparation);

        PendingCatImport pending;
        pending.id = token_.substr(0, std::min<std::size_t>(12, token_.size())) + "-" +
                     std::to_string(++importSequence_);
        pending.oldInventoryCount = inventory_.cats().size();
        pending.newInventoryCount = pending.oldInventoryCount;

        const auto inventoryPath = extracted / "commander_cat_inventory.csv";
        if (std::filesystem::exists(inventoryPath)) {
            const auto content = readFile(inventoryPath, true);
            if (content.empty() || !validUtf8(content))
                throw std::runtime_error("库存 CSV 不是有效的 UTF-8 文本");
            CommanderCatInventory imported(inventoryPath);
            imported.load();
            for (const auto& cat : imported.cats()) validateImportedCat(cat);
            pending.inventory = readFile(inventoryPath, true);
            pending.newInventoryCount = imported.cats().size();

            std::map<std::string, const InventoryCat*> before, after;
            for (const auto& cat : inventory_.cats()) before[cat.instanceId] = &cat;
            for (const auto& cat : imported.cats()) after[cat.instanceId] = &cat;
            for (const auto& [id, cat] : after) {
                const auto found = before.find(id);
                if (found == before.end()) pending.added.push_back(id);
                else if (!sameInventoryCat(*found->second, *cat)) pending.changed.push_back(id);
            }
            for (const auto& [id, cat] : before)
                if (!after.count(id)) pending.removed.push_back(id);
        }

        if (!pending.inventory && !pending.settings)
            throw std::runtime_error("没有找到可导入的数据文件");
        pendingImport_ = pending;
        return pendingImportJson(*pendingImport_);
    }

    std::string applyCatImport(const std::map<std::string, std::string>& values) {
        if (!pendingImport_ || values.at("import_id") != pendingImport_->id)
            throw std::runtime_error("导入预览已失效，请重新选择文件");

        struct Replacement {
            std::filesystem::path target, temporary, backup;
            std::string content;
            bool existed = false;
            bool committed = false;
        };
        const auto backupRoot = userDataDirectory() / "import_backups" /
            (timestampForPath() + "-" + pendingImport_->id);
        std::error_code error;
        std::filesystem::create_directories(backupRoot, error);
        if (error) throw std::runtime_error("无法创建导入备份目录：" + error.message());

        std::vector<Replacement> replacements;
        auto add = [&](const std::filesystem::path& target,
                       const std::optional<std::string>& content) {
            if (!content) return;
            Replacement item;
            item.target = target;
            item.temporary = target;
            item.temporary += ".windows-import.tmp";
            item.backup = backupRoot / target.filename();
            item.content = *content;
            item.existed = std::filesystem::exists(target);
            replacements.push_back(std::move(item));
        };
        add(inventory_.filePath(), pendingImport_->inventory);
        add(settings_.filePath(), pendingImport_->settings);

        try {
            for (auto& item : replacements) {
                if (item.existed) {
                    std::filesystem::copy_file(item.target, item.backup,
                        std::filesystem::copy_options::overwrite_existing, error);
                    if (error) throw std::runtime_error("备份现有数据失败：" + error.message());
                }
                writeFile(item.temporary, item.content);
            }
            for (auto& item : replacements) {
                error.clear();
                std::filesystem::rename(item.temporary, item.target, error);
                if (error) throw std::runtime_error("替换数据文件失败：" + error.message());
                item.committed = true;
            }
            inventory_.load();
            settings_.load();
        } catch (...) {
            for (auto& item : replacements) {
                std::filesystem::remove(item.temporary, error);
                if (!item.committed) continue;
                if (!item.existed) {
                    std::filesystem::remove(item.target, error);
                } else {
                    auto rollback = item.target;
                    rollback += ".rollback.tmp";
                    std::filesystem::copy_file(item.backup, rollback,
                        std::filesystem::copy_options::overwrite_existing, error);
                    if (!error) std::filesystem::rename(rollback, item.target, error);
                }
            }
            try { inventory_.load(); settings_.load(); } catch (...) {}
            throw;
        }

        const bool inventoryUpdated = pendingImport_->inventory.has_value();
        const bool settingsUpdated = pendingImport_->settings.has_value();
        pendingImport_.reset();
        return "{\"ok\":true,\"inventoryUpdated\":" +
            std::string(inventoryUpdated ? "true" : "false") +
            ",\"settingsUpdated\":" + (settingsUpdated ? "true" : "false") +
            ",\"backupDirectory\":" + json(backupRoot.string()) + '}';
    }

    std::vector<std::string> validateInitialTalents(
            const CommanderCatSpecies& species,
            const std::vector<std::string>& requested,
            bool allowOutsidePool) const {
        if (species.fixedRoute) {
            const auto route = database_.fixedRouteFor(species.catId);
            if (route.empty()) throw std::runtime_error("固定成长猫缺少初始路线数据");
            return route.front()->talentIds;
        }
        if (requested.empty()) throw std::runtime_error("请选择游戏中实际出现的初始天赋");
        std::set<std::string> families;
        int level1 = 0, level2 = 0;
        std::vector<std::string> outside;
        const auto& pool = database_.initialPoolFor(species.catId);
        for (const auto& id : requested) {
            const auto& talent = database_.requireTalent(id);
            if (talent.kind != TalentKind::Normal && talent.kind != TalentKind::Special)
                throw std::runtime_error("库存只能录入普通或特殊初始天赋");
            if (talent.kind == TalentKind::Special && talent.level != 1)
                throw std::runtime_error("特殊天赋固定为1级");
            if (talent.kind == TalentKind::Normal && (talent.level < 1 || talent.level > 2))
                throw std::runtime_error("普通初始天赋只能是1级或2级");
            if (!families.insert(talent.familyId).second)
                throw std::runtime_error("同一成长链不能重复选择");
            if (talent.kind == TalentKind::Normal && talent.level == 2) ++level2;
            else ++level1;
            if (!pool.count(id)) outside.push_back(talent.name);
        }
        bool valid = false;
        if (species.rarity == "SSR")
            valid = (level1 == 3 && level2 == 0) || (level1 == 2 && level2 == 1) ||
                    (level1 == 0 && level2 == 2);
        else if (species.rarity == "SR")
            valid = (level1 == 2 && level2 == 0) || (level1 == 0 && level2 == 1);
        else if (species.rarity == "R") valid = level1 == 1 && level2 == 0;
        if (!valid) throw std::runtime_error(species.rarity + " 初始等级结构不合法");
        if (!outside.empty() && !allowOutsidePool) {
            std::string message = "CONFIRM_POOL:以下天赋不在当前已观测初始池：";
            for (std::size_t i = 0; i < outside.size(); ++i) {
                if (i) message += " + ";
                message += outside[i];
            }
            message += "。初始池记录可能不完整，确认这是游戏中的实际结果后仍可保存。";
            throw std::runtime_error(message);
        }
        return requested;
    }

    std::string suggested(const PotentialAssessment& result,
                          const std::string& excludedInstance = {}) const {
        return assessor_.uniqueNickname(result, inventory_.cats(), excludedInstance);
    }

    PotentialAssessment potentialFor(const std::string& catId,
                                     const std::vector<std::string>& talents,
                                     const std::string& instanceId = {}) const {
        InventoryCat preview{instanceId.empty() ? "PREVIEW" : instanceId,
                             catId, "", talents, std::nullopt, {}};
        return assessor_.assessPotential(preview);
    }

    std::string assessmentPreview(const std::map<std::string, std::string>& values) const {
        const auto& species = database_.requireSpecies(values.at("cat_id"));
        const auto rawTalents = split(values.count("talents") ? values.at("talents") : "", ',');
        bool poolConfirmation = false;
        std::string poolWarning;
        std::vector<std::string> talents;
        try {
            talents = validateInitialTalents(species, rawTalents, false);
        } catch (const std::exception& error) {
            const std::string message = error.what();
            if (message.rfind("CONFIRM_POOL:", 0) != 0) throw;
            poolConfirmation = true;
            poolWarning = message.substr(std::string("CONFIRM_POOL:").size());
            talents = validateInitialTalents(species, rawTalents, true);
        }
        const std::string id = values.count("id") ? values.at("id") : "";
        const auto result = potentialFor(species.catId, talents, id);
        std::ostringstream out;
        out << "{\"valid\":true,\"nickname\":" << json(suggested(result, id))
            << ",\"poolConfirmation\":" << (poolConfirmation ? "true" : "false")
            << ",\"poolWarning\":" << json(poolWarning)
            << ",\"fixed\":" << (result.fixedGrowth ? "true" : "false")
            << ",\"feasibility\":" << result.score
            << ",\"feasibilityTier\":" << json(result.fixedGrowth ? "固定" : result.tier)
            << ",\"embryoExplanation\":" << json(result.embryoExplanation)
            << ",\"ceiling\":" << (result.ceilingQuantified ? std::to_string(result.ceilingScore) : "null")
            << ",\"ceilingTier\":" << json(result.ceilingQuantified ? result.ceilingTier : "U")
            << ",\"route\":" << json(result.hasRoute
                ? result.finalRoute.category + " / " + toText(result.finalRoute.slot) +
                  " / " + result.finalRoute.buildStyle : "暂无可用路线")
            << ",\"target\":" << json(result.hasRoute
                ? names(database_, result.finalRoute.targetTalentIds) : "无")
            << ",\"retained\":" << json(result.hasRoute
                ? names(database_, result.finalRoute.retainedTalentIds) : "无")
            << ",\"acquire\":" << json(result.hasRoute
                ? names(database_, result.finalRoute.acquireTalentIds) : "无")
            << ",\"upgrades\":" << json(result.hasRoute
                ? upgradeNames(database_, result.finalRoute.upgradeActions) : "无")
            << ",\"replacement\":" << json(result.hasRoute
                ? names(database_, result.finalRoute.replacementTalentIds) : "无")
            << ",\"points\":" << (result.hasRoute ? result.finalRoute.requiredPoints : 0)
            << ",\"routesDiffer\":" << (result.routesDiffer ? "true" : "false")
            << ",\"practicalRoute\":" << json(result.hasRoute
                ? result.practicalRoute.category + " / " + toText(result.practicalRoute.slot) +
                  " / " + result.practicalRoute.buildStyle : "暂无可用路线")
            << ",\"practicalFeasibility\":" << result.practicalRoute.feasibilityScore
            << ",\"practicalTier\":" << json(result.practicalRoute.feasibilityTier)
            << ",\"practicalTarget\":" << json(result.hasRoute
                ? names(database_, result.practicalRoute.targetTalentIds) : "无")
            << ",\"practicalPriority\":" << json(result.hasRoute
                ? names(database_, result.practicalRoute.priorityTalentIds) : "无")
            << ",\"practicalRetained\":" << json(result.hasRoute
                ? names(database_, result.practicalRoute.retainedTalentIds) : "无")
            << ",\"practicalAcquire\":" << json(result.hasRoute
                ? names(database_, result.practicalRoute.acquireTalentIds) : "无")
            << ",\"practicalUpgrades\":" << json(result.hasRoute
                ? upgradeNames(database_, result.practicalRoute.upgradeActions) : "无")
            << ",\"practicalReplacement\":" << json(result.hasRoute
                ? names(database_, result.practicalRoute.replacementTalentIds) : "无")
            << ",\"practicalPoints\":" << (result.hasRoute ? result.practicalRoute.requiredPoints : 0)
            << ",\"selected\":" << json(names(database_, talents)) << '}';
        return out.str();
    }

    std::string trainingPlanJson(const RouteAssessment& r) const {
        auto array=[](const std::vector<std::string>& ids) {
            std::string value="["; for(const auto& id:ids) {if(value.size()>1)value+=",";value+=json(id);} return value+"]";
        };
        std::ostringstream out;
        out << "{\"targetTalentIds\":" << array(r.targetTalentIds)
            << ",\"retainedTalentIds\":" << array(r.retainedTalentIds)
            << ",\"acquireTalentIds\":" << array(r.acquireTalentIds)
            << ",\"replacementTalentIds\":" << array(r.replacementTalentIds)
            << ",\"flexibleInitialTalentIds\":" << array(r.flexibleInitialTalentIds)
            << ",\"priorityTalentIds\":" << array(r.priorityTalentIds)
            << ",\"requiredPoints\":" << r.requiredPoints
            << ",\"reachable\":" << (r.reachable?"true":"false")
            << ",\"unreachableReason\":" << json(r.reachable ? "" : "目标路线无法在五槽、六点及学习限制内完成")
            << ",\"fixedGrowth\":" << (r.buildId.rfind("fixed",0)==0?"true":"false")
            << ",\"upgradeActions\":[";
        bool first=true;
        for(const auto& action:r.upgradeActions) {
            if(!first)out<<','; first=false;
            out << "{\"familyId\":" << json(action.pathTalentIds.empty()?"":database_.requireTalent(action.pathTalentIds.front()).familyId)
                << ",\"pathTalentIds\":" << array(action.pathTalentIds)
                << ",\"requiredPoints\":" << action.requiredPoints << '}';
        }
        out << "]}"; return out.str();
    }

    std::string pairMemberJson(const InventoryCat& cat, const RouteAssessment& route,
                               double fit, const std::string& label,
                               const std::string& slot) const {
        const auto& species = database_.requireSpecies(cat.catId);
        std::ostringstream out;
        out << "{\"id\":" << json(cat.instanceId) << ",\"catId\":" << json(cat.catId)
            << ",\"name\":" << json(species.name) << ",\"nickname\":" << json(cat.nickname)
            << ",\"slot\":" << json(slot) << ",\"style\":" << json(route.buildStyle)
            << ",\"fit\":" << fit << ",\"fitLabel\":" << json(label)
            << ",\"target\":" << json(names(database_, route.targetTalentIds))
            << ",\"priority\":" << json(names(database_, route.priorityTalentIds))
            << ",\"retained\":" << json(names(database_, route.retainedTalentIds))
            << ",\"acquire\":" << json(names(database_, route.acquireTalentIds))
            << ",\"upgrades\":" << json(upgradeNames(database_, route.upgradeActions))
            << ",\"replacement\":" << json(names(database_, route.replacementTalentIds))
            << ",\"points\":" << route.requiredPoints << '}';
        return out.str();
    }

    std::string snapshot() const {
        std::ostringstream out;
        out << "{\"token\":" << json(token_) << ",\"species\":[";
        bool first = true;
        for (const auto& cat : database_.species()) {
            if (!first) out << ',';
            first = false;
            const auto fixedRoute = database_.fixedRouteFor(cat.catId);
            const auto fixedInitial = fixedRoute.empty() ? std::vector<std::string>{}
                                                         : fixedRoute.front()->talentIds;
            out << "{\"id\":" << json(cat.catId) << ",\"name\":" << json(cat.name)
                << ",\"faction\":" << json(cat.faction) << ",\"rarity\":" << json(cat.rarity)
                << ",\"fixed\":" << (cat.fixedRoute ? "true" : "false")
                << ",\"fixedInitial\":" << json(names(database_, fixedInitial))
                << ",\"pool\":[";
            bool poolFirst = true;
            for (const auto& talentId : database_.initialPoolFor(cat.catId)) {
                if (!poolFirst) out << ',';
                poolFirst = false;
                out << json(talentId);
            }
            out << "]}";
        }
        out << "],\"talents\":[";
        first = true;
        for (const auto& talent : database_.talents()) {
            if (talent.kind == TalentKind::Fixed || talent.kind == TalentKind::Placeholder ||
                (talent.kind == TalentKind::Normal && talent.level > 2)) continue;
            if (!first) out << ',';
            first = false;
            out << "{\"id\":" << json(talent.talentId)
                << ",\"family\":" << json(talent.familyId)
                << ",\"name\":" << json(talent.name)
                << ",\"kind\":" << json(toText(talent.kind))
                << ",\"level\":" << talent.level << '}';
        }
        out << "],\"allTalents\":[";
        first=true;
        for(const auto& talent:database_.talents()) {
            if(talent.kind==TalentKind::Placeholder)continue;
            if(!first)out<<',';first=false;
            out<<"{\"id\":"<<json(talent.talentId)<<",\"name\":"<<json(talent.name)
               <<",\"family\":"<<json(talent.familyId)<<",\"level\":"<<talent.level<<'}';
        }
        out << "],\"inventory\":[";
        std::set<std::string> batchUsed;
        std::map<std::string, std::string> batchNames;
        // A scan may reorder CSV rows. Reserve valid existing numbers before
        // assigning new ones; sorting IDs makes collision repair deterministic.
        std::vector<const InventoryCat*> namingOrder;
        for (const auto& cat : inventory_.cats()) namingOrder.push_back(&cat);
        std::sort(namingOrder.begin(), namingOrder.end(), [](const auto* a, const auto* b) {
            return a->instanceId < b->instanceId;
        });
        std::map<std::string, std::string> nicknameBases;
        for (const auto* cat : namingOrder) {
            const auto base = assessor_.nicknameBase(assessor_.assessPotential(*cat));
            nicknameBases[cat->instanceId] = base;
            auto existing = cat->nickname;
            const std::string completed = "成";
            if (existing.size() >= completed.size() &&
                existing.compare(existing.size()-completed.size(), completed.size(), completed)==0)
                existing.resize(existing.size()-completed.size());
            if (existing.rfind(base, 0) != 0) continue;
            const auto suffix = existing.substr(base.size());
            const bool numbered = !suffix.empty() && suffix.front() != '0' &&
                std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; });
            if (numbered && database_.nicknameAllowed(existing) && batchUsed.insert(existing).second)
                batchNames[cat->instanceId] = existing;
        }
        for (const auto* cat : namingOrder) {
            if (batchNames.count(cat->instanceId)) continue;
            const auto& base = nicknameBases.at(cat->instanceId);
            for (std::size_t serial = 1;; ++serial) {
                const auto candidate = base + std::to_string(serial);
                if (database_.nicknameAllowed(candidate) && batchUsed.insert(candidate).second) {
                    batchNames[cat->instanceId] = candidate;
                    break;
                }
            }
        }
        first = true;
        for (const auto& cat : inventory_.cats()) {
            const auto& species = database_.requireSpecies(cat.catId);
            const auto result = assessor_.assessPotential(cat);
            if (!first) out << ',';
            first = false;
            out << "{\"id\":" << json(cat.instanceId) << ",\"catId\":" << json(cat.catId)
                << ",\"name\":" << json(species.name) << ",\"rarity\":" << json(species.rarity)
                << ",\"faction\":" << json(species.faction) << ",\"nickname\":" << json(cat.nickname)
                << ",\"batchNickname\":" << json(batchNames.at(cat.instanceId))
                << ",\"image\":" << json("/assets/commander_cats/" + cat.catId + ".png")
                << ",\"initial\":" << json(names(database_, cat.initialTalentIds))
                << ",\"initialIds\":[";
            for (std::size_t i = 0; i < cat.initialTalentIds.size(); ++i) {
                if (i) out << ',';
                out << json(cat.initialTalentIds[i]);
            }
            out << "],\"fixed\":" << (result.fixedGrowth ? "true" : "false")
                << ",\"feasibility\":" << result.score
                << ",\"feasibilityTier\":" << json(result.fixedGrowth ? "固定" : result.tier)
                << ",\"embryoExplanation\":" << json(result.embryoExplanation)
                << ",\"embryoUses\":" << embryoUsesJson(cat)
                << ",\"protectedRare\":" << (result.protectedRare ? "true" : "false")
                << ",\"trainingPlan\":" << trainingPlanJson(result.finalRoute)
                << ",\"practicalTrainingPlan\":" << trainingPlanJson(result.practicalRoute)
                << ",\"ruleVersion\":" << json(CAT_EMBRYO_RULE_VERSION)
                << ",\"ceiling\":" << (result.ceilingQuantified ? std::to_string(result.ceilingScore) : "null")
                << ",\"ceilingTier\":" << json(result.ceilingQuantified ? result.ceilingTier : "U")
                << ",\"near\":" << (result.nearGraduationStart ? "true" : "false");
            if (result.hasRoute) {
                out << ",\"category\":" << json(result.finalRoute.category)
                    << ",\"slot\":" << json(toText(result.finalRoute.slot))
                    << ",\"style\":" << json(result.finalRoute.buildStyle)
                    << ",\"target\":" << json(names(database_, result.finalRoute.targetTalentIds))
                    << ",\"priority\":" << json(names(database_, result.finalRoute.priorityTalentIds))
                    << ",\"retained\":" << json(names(database_, result.finalRoute.retainedTalentIds))
                    << ",\"acquire\":" << json(names(database_, result.finalRoute.acquireTalentIds))
                    << ",\"upgrades\":" << json(upgradeNames(database_, result.finalRoute.upgradeActions))
                    << ",\"replacement\":" << json(names(database_, result.finalRoute.replacementTalentIds))
                    << ",\"flexible\":" << json(names(database_, result.finalRoute.flexibleInitialTalentIds))
                    << ",\"junk\":" << json(names(database_, result.finalRoute.unrelatedInitialTalentIds))
                    << ",\"points\":" << result.finalRoute.requiredPoints
                    << ",\"finalRoute\":" << json(result.finalRoute.category + " / " +
                        toText(result.finalRoute.slot) + " / " + result.finalRoute.buildStyle)
                    << ",\"routesDiffer\":" << (result.routesDiffer ? "true" : "false")
                    << ",\"guide\":" << json(result.finalRoute.guideRecommendation)
                    << ",\"practicalRoute\":" << json(result.practicalRoute.category + " / " +
                        toText(result.practicalRoute.slot) + " / " + result.practicalRoute.buildStyle)
                    << ",\"practicalFeasibility\":" << result.practicalRoute.feasibilityScore
                    << ",\"practicalTier\":" << json(result.practicalRoute.feasibilityTier)
                    << ",\"practicalTarget\":" << json(names(database_, result.practicalRoute.targetTalentIds))
                    << ",\"practicalPriority\":" << json(names(database_, result.practicalRoute.priorityTalentIds))
                    << ",\"practicalRetained\":" << json(names(database_, result.practicalRoute.retainedTalentIds))
                    << ",\"practicalAcquire\":" << json(names(database_, result.practicalRoute.acquireTalentIds))
                    << ",\"practicalUpgrades\":" << json(upgradeNames(database_, result.practicalRoute.upgradeActions))
                    << ",\"practicalReplacement\":" << json(names(database_, result.practicalRoute.replacementTalentIds))
                    << ",\"practicalFlexible\":" << json(names(database_, result.practicalRoute.flexibleInitialTalentIds))
                    << ",\"practicalPoints\":" << result.practicalRoute.requiredPoints
                    << ",\"practicalGuide\":" << json(result.practicalRoute.guideRecommendation);
            }
            out << '}';
        }

        out << "]"
            << ",\"stats\":{\"species\":" << database_.species().size()
            << ",\"builds\":" << database_.builds().size()
            << ",\"rankings\":" << database_.rankings().size()
            << ",\"guides\":" << database_.guideRecommendations().size() << "}"
            << ",\"guideCount\":" << database_.guideRecommendations().size() << '}';
        return out.str();
    }

    std::string appSnapshot() const {
        const auto owned = std::count_if(shipProgress_.entries().begin(), shipProgress_.entries().end(),
            [](const auto& entry) { return entry.status != ShipStoryStatus::Unowned; });
        std::ostringstream out;
        out << "{\"token\":" << json(token_) << ",\"game\":\"azur_lane\",\"commanderCats\":"
            << inventory_.cats().size() << ",\"storyShips\":" << shipDatabase_.stories().size()
            << ",\"ownedStoryShips\":" << owned << '}';
        return out.str();
    }

    std::string shipStorySnapshot() const {
        std::ostringstream out;
        out << "{\"token\":" << json(token_) << ",\"ships\":[";
        bool first = true;
        for (const auto& story : shipDatabase_.stories()) {
            const auto& progress = shipProgress_.require(story.shipId);
            if (!first) out << ',';
            first = false;
            out << "{\"id\":" << json(story.shipId)
                << ",\"wikiName\":" << json(story.wikiName)
                << ",\"canonicalName\":" << json(story.canonicalName)
                << ",\"harmonyName\":" << json(story.harmonyName)
                << ",\"name\":" << json(story.displayLabel())
                << ",\"camp\":" << json(story.camp)
                << ",\"type\":" << json(story.shipType)
                << ",\"group\":" << json(story.fleetGroup())
                << ",\"storyTitle\":" << json(story.storyTitle)
                << ",\"image\":" << json("/assets/ships/" + story.imageFile)
                << ",\"status\":" << json(toText(progress.status))
                << ",\"maxLevel\":" << (progress.isMaxLevel ? "true" : "false")
                << ",\"updatedAt\":" << json(progress.updatedAt)
                << ",\"aliases\":[";
            for (std::size_t i = 0; i < story.aliases.size(); ++i) {
                if (i) out << ',';
                out << json(story.aliases[i]);
            }
            out << "],\"steps\":[";
            for (std::size_t i = 0; i < story.steps.size(); ++i) {
                if (i) out << ',';
                const auto& step = story.steps[i];
                out << "{\"order\":" << step.order
                    << ",\"title\":" << json(step.title)
                    << ",\"requirement\":" << json(step.requirement)
                    << ",\"reward\":" << json(step.reward) << '}';
            }
            out << "]}";
        }
        out << "],\"stats\":{\"total\":" << shipDatabase_.stories().size();
        for (const auto status : {ShipStoryStatus::Unowned, ShipStoryStatus::InProgress,
                                  ShipStoryStatus::Completed}) {
            const auto count = std::count_if(shipProgress_.entries().begin(),
                shipProgress_.entries().end(), [&](const auto& entry) { return entry.status == status; });
            out << ',' << json(toText(status)) << ':' << count;
        }
        out << "}}";
        return out.str();
    }

    bool authorized(const Request& request) const {
        const auto found = request.headers.find("X-Azur-Token");
        return found != request.headers.end() && found->second == token_;
    }

    std::pair<int, std::string> wikiRefresh(const std::string& flag) const {
        const std::string command = shellQuote(AZUR_LANE_PYTHON_EXECUTABLE) + " " +
            shellQuote(AZUR_LANE_WIKI_REFRESH_SCRIPT) + " " + flag + " >/dev/null 2>&1";
        const int status = std::system(command.c_str());
        auto result = readFile(AZUR_LANE_WIKI_REFRESH_PREVIEW);
        if (result.empty()) result = "{\"ok\":false,\"error\":\"刷新程序没有生成结果\"}";
        return {status, result};
    }

    std::pair<int, std::string> gameDataRefresh(const std::string& action,
                                                 const std::string& module) const {
        if (module != "ships" && module != "fleets")
            return {1, "{\"ok\":false,\"error\":\"未知的数据模块\"}"};
        const auto resultFile = cacheDirectory() / ("game_refresh_" + module + ".json");
        const std::string command = shellQuote(AZUR_LANE_PYTHON_EXECUTABLE) + " " +
            shellQuote(GAME_DATA_REFRESH_SCRIPT) + " " + action + " " + module +
            " --cache-dir " + shellQuote(cacheDirectory().string()) + " >" +
            shellQuote(resultFile.string()) + " 2>&1";
        const int status = std::system(command.c_str());
        auto result = readFile(resultFile);
        const auto marker = result.rfind("{\"ok\"");
        if (marker != std::string::npos) result = result.substr(marker);
        if (result.empty()) result = "{\"ok\":false,\"error\":\"刷新程序没有生成结果\"}";
        return {status, result};
    }

    void mutation(const Request& request) {
        if (!authorized(request)) throw std::runtime_error("invalid local session token");
        const auto values = form(request.body);
        std::string path = request.path;
        if (path.rfind("/api/commander-cats/", 0) == 0)
            path = "/api/" + path.substr(std::string("/api/commander-cats/").size());
        if (path == "/api/delete" || path == "/api/add" ||
                   path == "/api/update" || path == "/api/rename") {
            throw std::runtime_error("Mac 端库存为只读，请在 Windows 扫描后使用导入功能同步");
        } else throw std::runtime_error("unknown mutation");
    }

    void handle(int client) {
        const auto request = parseRequest(client);
        if (request.path == "/api/equipment/snapshot" && request.method == "GET") {
            const auto result = equipmentCommand("snapshot");
            return respond(client, result.first == 0 ? 200 : 400, "application/json; charset=utf-8", result.second);
        }
        if (request.method == "POST" && (request.path == "/api/equipment/save" ||
            request.path == "/api/equipment/export" || request.path == "/api/equipment/import-preview" ||
            request.path == "/api/equipment/import-apply")) {
            if (!authorized(request)) return respond(client, 403, "application/json; charset=utf-8",
                "{\"error\":\"invalid local session token\"}");
            const auto values = form(request.body);
            const auto result = equipmentCommand(request.path.substr(std::string("/api/equipment/").size()), values.at("payload"));
            return respond(client, result.first == 0 ? 200 : 400, "application/json; charset=utf-8", result.second);
        }
        if(request.path=="/api/commander-cats/offline"&&request.method=="GET"){
            const auto result=fleetCommand("offline");
            return respond(client,result.first==0?200:400,"application/json; charset=utf-8",result.second);
        }
        if(request.method=="POST"&&(request.path=="/api/commander-cats/fleets/import-preview"||request.path=="/api/commander-cats/fleets/import-apply")){
            if(!authorized(request))return respond(client,403,"application/json; charset=utf-8","{\"error\":\"invalid local session token\"}");
            try{
                const auto values=form(request.body);
                const auto result=fleetCommand(request.path.find("import-preview")!=std::string::npos?"import-preview":"import-apply",values.at("payload"));
                return respond(client,result.first==0?200:400,"application/json; charset=utf-8",result.second);
            }catch(const std::exception& e){return respond(client,400,"application/json; charset=utf-8","{\"error\":"+json(e.what())+"}");}
        }
        if(request.path=="/api/commander-cats/fleets"&&request.method=="GET"){
            const auto result=fleetCommand("snapshot");
            return respond(client,result.first==0?200:400,"application/json; charset=utf-8",result.second);
        }
        if(request.method=="POST"&&(request.path=="/api/commander-cats/fleets/save"||request.path=="/api/commander-cats/fleets/recommend"||request.path=="/api/commander-cats/fleets/theory"||request.path=="/api/commander-cats/fleets/export")){
            if(!authorized(request))return respond(client,403,"application/json; charset=utf-8","{\"error\":\"invalid local session token\"}");
            try {
                const auto values=form(request.body);
                const bool save=request.path=="/api/commander-cats/fleets/save";
                const bool exporting=request.path=="/api/commander-cats/fleets/export";
                const bool theory=request.path=="/api/commander-cats/fleets/theory";
                auto result=fleetCommand(save?"save":exporting?"export":theory?"theory":"recommend",values.at("payload"));
                if(!save&&!exporting&&result.first==0){
                    auto calculated=realFleetRecommendations(theory);
                    if(theory)return respond(client,200,"application/json; charset=utf-8",calculated);
                    writeFile(cacheDirectory()/("fleet-session-"+token_)/"computed.json",calculated);
                    result=fleetCommand("store-result",values.at("payload"));
                }
                return respond(client,result.first==0?200:400,"application/json; charset=utf-8",result.second);
            }catch(const std::exception& error){return respond(client,400,"application/json; charset=utf-8","{\"error\":"+json(error.what())+"}");}
        }
        if (request.method == "GET" && request.path == "/api/app/snapshot")
            return respond(client, 200, "application/json; charset=utf-8", appSnapshot());
        if (request.method == "GET" &&
            (request.path == "/api/snapshot" || request.path == "/api/commander-cats/snapshot"))
            return respond(client, 200, "application/json; charset=utf-8", snapshot());
        if (request.method == "GET" && request.path == "/api/ship-stories/snapshot")
            return respond(client, 200, "application/json; charset=utf-8", shipStorySnapshot());
        if (request.method == "POST" &&
            (request.path == "/api/preview" || request.path == "/api/commander-cats/preview")) {
            if (!authorized(request))
                return respond(client, 403, "application/json; charset=utf-8",
                               "{\"error\":\"invalid local session token\"}");
            try {
                return respond(client, 200, "application/json; charset=utf-8",
                               assessmentPreview(form(request.body)));
            } catch (const std::exception& error) {
                return respond(client, 400, "application/json; charset=utf-8",
                               "{\"error\":" + json(error.what()) + "}");
            }
        }
        if (request.method == "POST" &&
            (request.path == "/api/commander-cats/import-preview" ||
             request.path == "/api/commander-cats/import-apply")) {
            if (!authorized(request))
                return respond(client, 403, "application/json; charset=utf-8",
                               "{\"error\":\"invalid local session token\"}");
            try {
                const auto values = form(request.body);
                const auto result = request.path == "/api/commander-cats/import-preview"
                    ? prepareCatImport(values) : applyCatImport(values);
                return respond(client, 200, "application/json; charset=utf-8", result);
            } catch (const std::exception& error) {
                return respond(client, 400, "application/json; charset=utf-8",
                               "{\"error\":" + json(error.what()) + "}");
            }
        }
        if (request.method == "POST" &&
            (request.path == "/api/wiki-preview" || request.path == "/api/wiki-apply")) {
            if (!authorized(request))
                return respond(client, 403, "application/json; charset=utf-8",
                               "{\"error\":\"invalid local session token\"}");
            const auto result = wikiRefresh(request.path == "/api/wiki-preview" ? "--prepare" : "--apply");
            return respond(client, result.first == 0 ? 200 : 400,
                           "application/json; charset=utf-8", result.second);
        }
        if (request.method == "POST" && request.path == "/api/ship-stories/progress") {
            if (!authorized(request))
                return respond(client, 403, "application/json; charset=utf-8",
                               "{\"error\":\"invalid local session token\"}");
            try {
                const auto values = form(request.body);
                const auto status = shipStoryStatusFromText(values.at("status"));
                if (!status) throw std::runtime_error("未知的剧情状态");
                std::optional<bool> maxLevel;
                if (values.count("is_max_level")) {
                    if (values.at("is_max_level") != "true" && values.at("is_max_level") != "false")
                        throw std::runtime_error("满级状态必须是 true 或 false");
                    maxLevel = values.at("is_max_level") == "true";
                }
                shipProgress_.update(values.at("ship_id"), *status, maxLevel);
                return respond(client, 200, "application/json; charset=utf-8", "{\"ok\":true}");
            } catch (const std::exception& error) {
                return respond(client, 400, "application/json; charset=utf-8",
                               "{\"error\":" + json(error.what()) + "}");
            }
        }
        if (request.method == "POST" &&
            (request.path == "/api/data-refresh/preview" ||
             request.path == "/api/data-refresh/apply")) {
            if (!authorized(request))
                return respond(client, 403, "application/json; charset=utf-8",
                               "{\"error\":\"invalid local session token\"}");
            const auto values = form(request.body);
            const auto module = values.count("module") ? values.at("module") : "";
            const auto result = gameDataRefresh(
                request.path == "/api/data-refresh/preview" ? "prepare" : "apply", module);
            return respond(client, result.first == 0 ? 200 : 400,
                           "application/json; charset=utf-8", result.second);
        }
        const bool catMutation = request.path == "/api/delete" || request.path == "/api/add" ||
            request.path == "/api/update" || request.path == "/api/rename" ||
            request.path == "/api/settings" ||
            request.path == "/api/commander-cats/delete" ||
            request.path == "/api/commander-cats/add" ||
            request.path == "/api/commander-cats/update" ||
            request.path == "/api/commander-cats/rename" ||
            request.path == "/api/commander-cats/settings";
        if (request.method == "POST" && catMutation) {
            try {
                mutation(request);
                respond(client, 200, "application/json; charset=utf-8", "{\"ok\":true}");
            } catch (const std::exception& error) {
                respond(client, 400, "application/json; charset=utf-8",
                        "{\"error\":" + json(error.what()) + "}");
            }
            return;
        }

        const std::filesystem::path base = GAME_GUI_WEB_DIR;
        std::filesystem::path file;
        if (request.path == "/") file = base / "index.html";
        else if (request.path == "/style.css") file = base / "style.css";
        else if (request.path == "/app.js") file = base / "app.js";
        else if (request.path == "/placeholder.svg") file = base / "placeholder.svg";
        else if (request.path == "/equipment.js") file = base / "equipment.js";
        else if (request.path == "/equipment.css") file = base / "equipment.css";
        else if (request.path == "/fleet_plans.js") file = base / "fleet_plans.js";
        else if (request.path == "/training_plan.js") file = base / "training_plan.js";
        else if (request.path == "/cat_offline.js") file = base / "cat_offline.js";
        else if (request.path.find("..") == std::string::npos && request.path.rfind("/assets/commander_cats/", 0) == 0)
            file = std::filesystem::path(GAME_DATA_DIR) / "assets/commander_cats" / request.path.substr(std::string("/assets/commander_cats/").size());
        else if (request.path.find("..") == std::string::npos && request.path.rfind("/assets/ships/", 0) == 0)
            file = std::filesystem::path(GAME_DATA_DIR) / "assets/ships" / request.path.substr(std::string("/assets/ships/").size());
        else if (request.path.find("..") == std::string::npos && request.path.rfind("/assets/equipment/", 0) == 0)
            file = std::filesystem::path(GAME_DATA_DIR) / "equipment/assets" / request.path.substr(std::string("/assets/equipment/").size());
        else if (request.path.find("..") == std::string::npos && request.path.rfind("/assets/fleet-ships/", 0) == 0)
            file = std::filesystem::path(GAME_DATA_DIR) / "fleet_ships/portraits" / request.path.substr(std::string("/assets/fleet-ships/").size());
        else if (request.path.rfind("/assets/cat-evidence/",0)==0) {
            const auto name=request.path.substr(std::string("/assets/cat-evidence/").size());
            if(name.size()!=68 || name.substr(64)!=".png" ||
               !std::all_of(name.begin(),name.begin()+64,[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))
                return respond(client,404,"text/plain","not found");
            file=userDataDirectory()/"graduation_images"/name;
        }
        else return respond(client, 404, "application/json; charset=utf-8", "{\"error\":\"not found\"}");
        if (!std::filesystem::exists(file) && request.path.rfind("/assets/", 0) == 0)
            file = base / "placeholder.svg";
        const auto content = readFile(file, true);
        if (content.empty()) return respond(client, 404, "text/plain", "not found");
        const auto extension = file.extension().string();
        const std::string type = extension == ".html" ? "text/html; charset=utf-8" :
            extension == ".json" ? "application/json; charset=utf-8" :
            extension == ".js" ? "application/javascript; charset=utf-8" :
            extension == ".css" ? "text/css; charset=utf-8" :
            extension == ".png" ? "image/png" :
            (extension == ".jpg" || extension == ".jpeg") ? "image/jpeg" : "image/svg+xml";
        respond(client, 200, type, content);
    }

    CommanderCatDatabase database_;
    CommanderCatInventory inventory_;
    CommanderCatSettings settings_;
    CatAssessor assessor_;
    CatPairRecommender pairer_;
    ShipStoryDatabase shipDatabase_;
    ShipStoryProgress shipProgress_;
    std::string token_;
    std::optional<PendingCatImport> pendingImport_;
    std::size_t importSequence_ = 0;
};

}  // namespace

int main() {
    try { GuiApp app; app.run(); return 0; }
    catch (const std::exception& error) {
        std::cerr << "无法启动碧蓝航线助手：" << error.what() << '\n';
        return 1;
    }
}
