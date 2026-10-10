#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/SaveSystem.h"
#include "ai_sim/TomlIO.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
std::string text(const toml::table& t,const char* key){return t[key].value_or(std::string{});}
std::string safe(const std::string& value){
    const char* digits="0123456789abcdef";std::string result;
    for(const unsigned char c:value){if(std::isalnum(c)||c=='_'||c=='-'||c=='.')result+=static_cast<char>(c);else{result+='~';result+=digits[c>>4];result+=digits[c&15];}}
    if(result.empty()||result=="."||result=="..")result="record-"+result;
    return result;
}
void write(const std::filesystem::path& root,const std::filesystem::path& relative,const toml::table& doc){
    std::filesystem::create_directories((root/relative).parent_path());write_toml(root/relative,doc);
}
std::filesystem::path file_path(const toml::table& entry){
    auto path=FileSystem::normalize(text(entry,"path"));
    std::filesystem::path result=safe(text(entry,"host_id"));
    if(path=="/")return result/"@root.toml";
    std::size_t offset=1;
    while(offset<path.size()){
        const auto slash=path.find('/',offset);result/=safe(path.substr(offset,slash-offset));
        if(slash==std::string::npos)break;
        offset=slash+1;
    }
    result+=".toml";return result;
}
void records(const std::filesystem::path& root,const std::filesystem::path& directory,
    const std::string& key,const toml::array& rows,toml::array& sources,int version){
    for(const auto& node:rows){const auto& row=*node.as_table();const auto name=safe(text(row,"id"))+".toml";
        write(root,directory/name,toml::table{{"schema_version",version},{key,toml::array{row}}});sources.push_back(name);
    }
}
void convert(const std::filesystem::path& source,const std::filesystem::path& output){
    const auto legacy=source/"scenarios/hobbyist";
    auto machine=toml::parse_file((legacy/"computer.toml").string());
    std::unordered_set<std::string> appnames;
    for(auto& node:*machine["processes"].as_array()){
        auto& instance=*node.as_table();auto app=instance;
        const auto* details=app["details"].as_table();
        auto stem=safe(details ? text(*details,"program_id") : text(app,"id"));
        if(!appnames.insert(stem).second){stem+="-"+safe(text(app,"id"));if(!appnames.insert(stem).second)throw std::runtime_error("Duplicate application filename");}
        const auto host_id=details ? text(*details,"host_id") : std::string{};
        app.erase("id");app.erase("owner");app.erase("ai_owned");
        if(auto* defaults=app["details"].as_table())defaults->erase("host_id");
        write(output,"applications/"+stem+".toml",toml::table{{"schema_version",1},{"application",app}});
        auto minimal=toml::table{{"id",text(instance,"id")},{"owner",text(instance,"owner")},
            {"ai_owned",instance["ai_owned"].value_or(false)},{"application","../applications/"+stem+".toml"}};
        if(!host_id.empty())minimal.insert("details",toml::table{{"host_id",host_id}});
        instance=std::move(minimal);
    }
    write(output,"computers/engineer_pc.toml",machine);
    auto network=toml::parse_file((legacy/"network.toml").string());
    toml::array hostsources;
    for(const auto& node:*network["hosts"].as_array()){
        const auto& host=*node.as_table();const auto name=safe(text(host,"id"))+".toml";
        write(output,"computers/hosts/"+name,toml::table{{"schema_version",1},{"hosts",toml::array{host}}});
        hostsources.push_back("../computers/hosts/"+name);
    }
    network.erase("hosts");network.insert_or_assign("sources",hostsources);write(output,"networks/office.toml",network);
    auto people=toml::parse_file((legacy/"people.toml").string());
    const bool haswork=std::filesystem::exists(legacy/"workday.toml");
    toml::table work;
    if(haswork){
        work=toml::parse_file((legacy/"workday.toml").string());
        for(const auto& n:*work["people"].as_array()){
            const auto& profile=*n.as_table();auto& target=*people["people"].as_array();
            auto found=std::find_if(target.begin(),target.end(),[&](auto& row){return text(*row.as_table(),"id")==text(profile,"id");});
            if(found==target.end())target.push_back(profile);
            else for(const auto& [key,value]:profile)if(!found->as_table()->contains(key.str()))found->as_table()->insert(key.str(),value);
        }
    }
    toml::array peoplesources;records(output,"people","people",*people["people"].as_array(),peoplesources,1);
    people.erase("people");people.insert_or_assign("sources",peoplesources);write(output,"people/office.toml",people);
    auto files=toml::parse_file((legacy/"files.toml").string());toml::array filesources;
    for(const auto& node:*files["entries"].as_array()){
        const auto path=file_path(*node.as_table());write(output,std::filesystem::path("files")/path,
            toml::table{{"schema_version",2},{"entries",toml::array{*node.as_table()}}});filesources.push_back(path.generic_string());
    }
    if(haswork){
        auto document=*work["document"].as_table();document.insert_or_assign("kind","file");const auto relative=file_path(document);
        // Reuse an existing definition for the same host/path rather than replace it.
        if(std::none_of(filesources.begin(),filesources.end(),[&](const toml::node& n){return n.value<std::string>()==relative.generic_string();})){
            write(output,std::filesystem::path("files")/relative,toml::table{{"schema_version",2},{"entries",toml::array{document}}});filesources.push_back(relative.generic_string());
        }
        work.insert_or_assign("document","../files/"+relative.generic_string());
        toml::array worksources{"../people/office.toml"};toml::array mailsources;
        records(output,"emails/workflows","emails",*work["emails"].as_array(),mailsources,1);
        for(const auto& n:mailsources)worksources.push_back("../emails/workflows/"+n.value_or(std::string{}));
        work.erase("people");work.erase("emails");work.insert_or_assign("sources",worksources);write(output,"workflows/handover.toml",work);
    }
    files.erase("entries");files.insert_or_assign("sources",filesources);write(output,"files/office.toml",files);
    auto emails=toml::parse_file((legacy/"emails.toml").string());toml::array mailsources;
    records(output,"emails/requests","emails",*emails["emails"].as_array(),mailsources,1);
    toml::array prefixed;for(const auto& n:mailsources)prefixed.push_back("requests/"+n.value_or(std::string{}));mailsources=std::move(prefixed);
    emails.erase("emails");emails.insert_or_assign("sources",mailsources);write(output,"emails/office.toml",emails);
    if(std::filesystem::exists(source/"news.toml")){
        auto news=toml::parse_file((source/"news.toml").string());toml::array newssources;
        for(const auto& n:*news["articles"].as_array()){
            if(!n.is_table())throw std::runtime_error("Invalid legacy news record");
            const auto& row=*n.as_table();if(text(row,"id").empty())throw std::runtime_error("Legacy news record is missing an ID");const auto category=safe(text(row,"category"));const auto name=safe(text(row,"id"))+".toml";
            write(output,"news/"+category+"/"+name,toml::table{{"schema_version",1},{"articles",toml::array{row}}});newssources.push_back(category+"/"+name);
        }
        news.erase("articles");news.insert_or_assign("sources",newssources);write(output,"news/feed.toml",news);
    }
    write(output,"places/office.toml",toml::table{{"schema_version",1},{"places",toml::array{toml::table{{"id","office"},{"name","Technology office"},{"kind","workplace"}}}}});
    auto manifest=toml::parse_file((legacy/"scenario.toml").string());
    toml::table defs{{"computer","../computers/engineer_pc.toml"},{"network","../networks/office.toml"},
        {"files","../files/office.toml"},{"people","../people/office.toml"},{"emails","../emails/office.toml"},
        {"places","../places/office.toml"}};
    if(std::filesystem::exists(output/"news/feed.toml"))defs.insert("news","../news/feed.toml");
    if(haswork)defs.insert("workday","../workflows/handover.toml");
    manifest.insert_or_assign("definitions",defs);write(output,"scenarios/hobbyist.toml",manifest);
    std::filesystem::remove_all(output/"scenarios/hobbyist");std::filesystem::remove(output/"news.toml");
}
}
bool DefinitionLoader::migrate_legacy(const std::filesystem::path& data_root){
    const auto legacy=data_root/"scenarios/hobbyist";
    if(!std::filesystem::is_regular_file(legacy/"computer.toml"))return false;
    const auto serial=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto stage=data_root.parent_path()/("data-layout-pending-"+serial);
    const auto backup=data_root.parent_path()/"backups"/("data-layout-"+serial);
    try{
        (void)SaveSystem::new_game(legacy/"computer.toml");
        std::filesystem::copy(data_root,stage,std::filesystem::copy_options::recursive);
        convert(data_root,stage);
        (void)SaveSystem::new_game(stage/"scenarios/hobbyist.toml");
        std::filesystem::create_directories(backup.parent_path());
        std::filesystem::rename(data_root,backup);
        try{std::filesystem::rename(stage,data_root);}catch(...){std::filesystem::rename(backup,data_root);throw;}
    }catch(...){std::error_code error;std::filesystem::remove_all(stage,error);throw;}
    return true;
}
}
