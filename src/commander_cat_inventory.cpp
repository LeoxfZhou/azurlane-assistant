#include "commander_cat_inventory.hpp"
#include "commander_cat_atomic.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace {

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields; std::string field; bool quoted=false;
    for(std::size_t i=0;i<line.size();++i){const char c=line[i];if(quoted){if(c=='"'&&i+1<line.size()&&line[i+1]=='"'){field.push_back('"');++i;}else if(c=='"')quoted=false;else field.push_back(c);}else if(c=='"')quoted=true;else if(c==','){fields.push_back(field);field.clear();}else field.push_back(c);}if(quoted)throw CommanderCatInventoryError("库存 CSV 引号没有闭合");fields.push_back(field);return fields;
}

std::string quoteCsv(const std::string& value) {
    if(value.find_first_of(",\"\r\n")==std::string::npos)return value;
    std::string result="\"";for(char c:value){if(c=='\"')result+="\"\"";else result.push_back(c);}return result+="\"";
}

std::vector<std::string> splitTalents(const std::string& value) {
    std::vector<std::string> result; if(value.empty())return result;std::size_t start=0;
    while(true){const auto found=value.find(" + ",start);result.push_back(value.substr(start,found-start));if(found==std::string::npos)break;start=found+3;}return result;
}

std::string joinTalents(const std::vector<std::string>& values) {
    std::string result;for(std::size_t i=0;i<values.size();++i){if(i)result+=" + ";result+=values[i];}return result;
}

}  // namespace

CommanderCatInventory::CommanderCatInventory(std::filesystem::path filePath):filePath_(std::move(filePath)){}

void CommanderCatInventory::load(){
    cats_.clear();if(!std::filesystem::exists(filePath_))return;std::ifstream input(filePath_);if(!input)throw CommanderCatInventoryError("无法打开库存文件："+filePath_.string());
    std::string line;if(!std::getline(input,line))throw CommanderCatInventoryError("库存文件为空");
    if(!line.empty()&&line.back()=='\r')line.pop_back();const bool facts=line=="instance_id,cat_id,nickname,initial_talent_ids";const bool legacy=line=="instance_id,cat_id,nickname,initial_talent_ids,created_at";const bool scored=line=="instance_id,cat_id,nickname,initial_talent_ids,potential_score,potential_tier";if(!facts&&!legacy&&!scored)throw CommanderCatInventoryError("库存文件表头损坏");
    std::size_t number=1;while(std::getline(input,line)){++number;if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;const auto fields=parseCsvLine(line);const std::size_t expected=facts?4:(legacy?5:6);if(fields.size()!=expected)throw CommanderCatInventoryError("库存文件第 "+std::to_string(number)+" 行字段数量错误");InventoryCat cat{fields[0],fields[1],fields[2],splitTalents(fields[3]),std::nullopt,{}};if(cat.instanceId.empty()||cat.catId.empty())throw CommanderCatInventoryError("库存文件第 "+std::to_string(number)+" 行关键字段为空");if(std::any_of(cats_.begin(),cats_.end(),[&cat](const auto& x){return x.instanceId==cat.instanceId;}))throw CommanderCatInventoryError("库存实例编号重复："+cat.instanceId);cats_.push_back(std::move(cat));}
    input.close();
    if(legacy||scored)save();
}

void CommanderCatInventory::save() const{
    std::error_code error;const auto parent=filePath_.parent_path();if(!parent.empty())std::filesystem::create_directories(parent,error);if(error)throw CommanderCatInventoryError("无法创建库存目录："+error.message());
    auto temporary=filePath_;temporary+=".tmp";{std::ofstream output(temporary,std::ios::trunc);if(!output)throw CommanderCatInventoryError("无法写入临时库存文件："+temporary.string());output<<"instance_id,cat_id,nickname,initial_talent_ids\n";for(const auto& cat:cats_)output<<quoteCsv(cat.instanceId)<<','<<quoteCsv(cat.catId)<<','<<quoteCsv(cat.nickname)<<','<<quoteCsv(joinTalents(cat.initialTalentIds))<<'\n';output.flush();if(!output)throw CommanderCatInventoryError("写入临时库存文件失败");}
    try { replaceCatFileWithBackup(temporary, filePath_); }
    catch (const std::exception& failure) {
        throw CommanderCatInventoryError(std::string("原子替换库存失败：") + failure.what());
    }
}

const std::vector<InventoryCat>& CommanderCatInventory::cats() const noexcept{return cats_;}

InventoryCat& CommanderCatInventory::add(const std::string& catId,std::string nickname,std::vector<std::string> talents,std::optional<double>,std::string){cats_.push_back({nextInstanceId(),catId,std::move(nickname),std::move(talents),std::nullopt,{}});save();return cats_.back();}

void CommanderCatInventory::update(const std::string& id,const std::string& catId,std::string nickname,std::vector<std::string> talents,std::optional<double>,std::string){auto it=std::find_if(cats_.begin(),cats_.end(),[&id](const auto& x){return x.instanceId==id;});if(it==cats_.end())throw CommanderCatInventoryError("找不到库存实例："+id);it->catId=catId;it->nickname=std::move(nickname);it->initialTalentIds=std::move(talents);it->potentialScore.reset();it->potentialTier.clear();save();}

void CommanderCatInventory::updateMetadata(const std::string& id,std::string nickname,std::optional<double>,std::string,bool saveImmediately){auto it=std::find_if(cats_.begin(),cats_.end(),[&id](const auto& x){return x.instanceId==id;});if(it==cats_.end())throw CommanderCatInventoryError("找不到库存实例："+id);it->nickname=std::move(nickname);it->potentialScore.reset();it->potentialTier.clear();if(saveImmediately)save();}

void CommanderCatInventory::remove(const std::string& id){const auto old=cats_.size();cats_.erase(std::remove_if(cats_.begin(),cats_.end(),[&id](const auto& x){return x.instanceId==id;}),cats_.end());if(cats_.size()==old)throw CommanderCatInventoryError("找不到库存实例："+id);save();}
const InventoryCat* CommanderCatInventory::find(const std::string& id) const noexcept{const auto it=std::find_if(cats_.begin(),cats_.end(),[&id](const auto& x){return x.instanceId==id;});return it==cats_.end()?nullptr:&*it;}
const std::filesystem::path& CommanderCatInventory::filePath() const noexcept{return filePath_;}

std::string CommanderCatInventory::nextInstanceId() const {
    int maximum=0;
    auto consume=[&](const std::string& id){if(id.rfind("CAT",0)==0){try{maximum=std::max(maximum,std::stoi(id.substr(3)));}catch(...){}}};
    for(const auto& cat:cats_)consume(cat.instanceId);
    const auto parent=filePath_.parent_path();
    if(std::filesystem::exists(parent))for(const auto& entry:std::filesystem::directory_iterator(parent)) {
        if(entry.path().filename().string().rfind(filePath_.filename().string()+".backup-",0)!=0)continue;
        std::ifstream input(entry.path());std::string line;
        while(std::getline(input,line)){auto fields=parseCsvLine(line);if(!fields.empty())consume(fields.front());}
    }
    const auto counter=parent/"commander_cat_instance_sequence.txt";
    {std::ifstream input(counter);int previous=0;if(input>>previous)maximum=std::max(maximum,previous);}
    std::filesystem::create_directories(parent);
    auto temporary=counter;temporary+=".tmp";
    {std::ofstream out(temporary);out<<(maximum+1);out.flush();if(!out)throw CommanderCatInventoryError("无法保留新实例编号");}
    replaceCatFileWithBackup(temporary,counter);
    std::ostringstream id;id<<"CAT"<<std::setw(4)<<std::setfill('0')<<(maximum+1);return id.str();
}
