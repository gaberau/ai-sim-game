#include "ai_sim/ConversationRules.h"
#include "ai_sim/DefinitionLoader.h"
#include <cmath>
#include <stdexcept>
#include <limits>
#include <unordered_set>
#include <vector>
#include <optional>

namespace ai_sim {
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(std::string{"Conversation rules: "}+message);}
std::string text(const toml::table& table,const char* key){return table[key].value_or(std::string{});}
const toml::table* declarations(const toml::table& definition){return definition["conversation"]["variables"].as_table();}
const toml::table* character(const toml::table& people,const std::string& id){
    if(id=="self:assistant")return people["ai_character"].as_table();
    if(auto* list=people["people"].as_array())for(const auto& n:*list)
        if(n.is_table() && text(*n.as_table(),"id")==id)return n.as_table();
    return nullptr;
}
toml::table* character(toml::table& people,const std::string& id){
    return const_cast<toml::table*>(character(static_cast<const toml::table&>(people),id));
}
std::pair<std::string,std::string> split(const std::string& reference){
    const auto dot=reference.rfind('.');require(dot!=std::string::npos && dot>0 && dot+1<reference.size(),"Use character_id.field for a state reference");
    return {reference.substr(0,dot),reference.substr(dot+1)};
}
const toml::table& specification(const toml::table& definition,const std::string& name,const toml::table* people=nullptr){
    if(name.find('.')!=std::string::npos){
        auto [id,field]=split(name);
        require(people!=nullptr,"Character reference needs world state");const auto* owner=character(*people,id);
        require(owner && (*owner)["state_definitions"][field].is_table(),"Unknown character state field");
        return *(*owner)["state_definitions"][field].as_table();
    }
    const auto* list=declarations(definition);require(list && (*list)[name].is_table(),"Unknown legacy variable");return *(*list)[name].as_table();
}
void reference(const toml::table& definition,const std::string& name,const toml::table* people){
    if(name.find('.')!=std::string::npos && !people){(void)split(name);return;}
    (void)specification(definition,name,people);
}
void bounded(const toml::table& spec,double value){
    require(std::isfinite(value),"Value must be finite");
    if(auto limit=spec["min"].value<double>())require(value>=*limit,"Value below minimum");
    if(auto limit=spec["max"].value<double>())require(value<=*limit,"Value above maximum");
    if(spec["integer"].value_or(false))require(std::floor(value)==value,"Counter must be integral");
}
void operand(const toml::table& definition,const toml::table& rule,const toml::table* people){
    require(rule.contains("value")!=rule.contains("other"),"Choose a numeric value or another variable");
    if(rule.contains("value")){auto value=rule["value"].value<double>();require(value && std::isfinite(*value),"Invalid numeric operand");}
    else {require(rule["other"].is_string(),"Invalid variable operand");reference(definition,text(rule,"other"),people);}
}
void test(const toml::table& definition,const toml::node& value,const toml::table* people){
    require(value.is_table(),"Test must be a table");const auto& rule=*value.as_table();
    reference(definition,text(rule,"variable"),people);operand(definition,rule,people);
    static const std::unordered_set<std::string> ops{"lt","le","eq","ne","ge","gt"};
    require(ops.contains(text(rule,"op")),"Unknown comparison operator");
}
double rhs(const toml::table& people,const toml::table& rule){
    if(auto value=rule["value"].value<double>())return *value;
    return ConversationRules::get(people,text(rule,"other"));
}
bool compare(const toml::table& people,const toml::table& rule){
    const double a=ConversationRules::get(people,text(rule,"variable")),b=rhs(people,rule);const auto op=text(rule,"op");
    if(op=="lt")return a<b;
    if(op=="le")return a<=b;
    if(op=="eq")return a==b;
    if(op=="ne")return a!=b;
    if(op=="ge")return a>=b;
    if(op=="gt")return a>b;
    throw std::runtime_error("Conversation rules: Unknown comparison operator");
}
void validate_entry(const toml::table& definition,const toml::table& entry,bool choice,const toml::table* people){
    for(auto key:{"test","requires_test"})if(entry.contains(key))test(definition,*entry.get(key),people);
    if(entry.contains("test"))require(!choice && text(entry,"kind")=="condition","test belongs on a condition node");
    if(entry.contains("effects")){
        const auto* list=entry["effects"].as_array();require(list!=nullptr,"effects must be an array");
        for(const auto& value:*list){require(value.is_table(),"Effect must be a table");const auto& rule=*value.as_table();
            reference(definition,text(rule,"variable"),people);operand(definition,rule,people);
            require(text(rule,"op")=="add" || text(rule,"op")=="set","Unknown numeric effect");
        }
    }
    for(auto key:{"restart","stop","shutdown"})if(entry.contains(key)){
        require(entry[key].is_boolean(),"Lifecycle effect must be boolean");
        const std::string expected=std::string{key}=="stop"?"end":"event";
        require(!choice && text(entry,"kind")==expected,"restart belongs on an event; stop belongs on an end node");
    }
}
}
void ConversationRules::validate_definition(const toml::table& definition,const toml::table* people){
    if(definition["conversation"].is_table() && definition["conversation"].as_table()->contains("variables")){
        const auto* list=declarations(definition);require(list!=nullptr,"Variable declarations must be a table");
        for(const auto& [key,value]:*list){require(!key.str().empty() && value.is_table(),"Invalid variable declaration");const auto& spec=*value.as_table();
            const auto initial=spec["initial"].value<double>();require(initial && std::isfinite(*initial),"Variable needs a finite initial value");
            for(auto bound:{"min","max"})if(spec.contains(bound)){auto n=spec[bound].value<double>();require(n && std::isfinite(*n),"Invalid variable bound");}
            if(spec.contains("integer"))require(spec["integer"].is_boolean(),"integer must be boolean");
            require(spec["min"].value_or(-std::numeric_limits<double>::infinity())<=spec["max"].value_or(std::numeric_limits<double>::infinity()),"Reversed variable bounds");bounded(spec,*initial);
        }
    }
    if(auto* nodes=definition["nodes"].as_array())for(const auto& value:*nodes){
        require(value.is_table(),"Invalid node");const auto& entry=*value.as_table();validate_entry(definition,entry,false,people);
        if(auto* choices=entry["choices"].as_array())for(const auto& choice:*choices){require(choice.is_table(),"Invalid choice");validate_entry(definition,*choice.as_table(),true,people);}
    }
}
double ConversationRules::get(const toml::table& people,const std::string& name){
    std::optional<double> value;
    if(name.find('.')!=std::string::npos){auto [id,field]=split(name);const auto* owner=character(people,id);require(owner!=nullptr,"Unknown state owner");value=(*owner)["state"][field].value<double>();}
    else value=people["chat"]["variables"][name].value<double>();
    require(value && std::isfinite(*value),"Missing/non-finite state value");return *value;
}
void ConversationRules::install(toml::table& people,const std::filesystem::path& directory){
    // Character declarations are loaded once; saved values/specifications remain authoritative.
    for(const auto& filename:{"mark.toml","assistant.toml"}) {
        const auto path=directory/filename;if(!std::filesystem::exists(path))continue;
        const auto document=DefinitionLoader::load(path);
        std::vector<const toml::table*> owners;
        if(auto* list=document["people"].as_array())for(const auto& n:*list)if(n.is_table())owners.push_back(n.as_table());
        if(auto* ai=document["ai_character"].as_table())owners.push_back(ai);
        for(const auto* source:owners){const auto id=text(*source,"id");
            auto* owner=character(people,id);
            if(!owner && id=="self:assistant"){people.insert("ai_character",*source);owner=people["ai_character"].as_table();}
            if(!owner)continue;
            if(auto* defs=(*source)["state_definitions"].as_table()) {
                if(!owner->contains("state_definitions"))owner->insert("state_definitions",toml::table{});
                auto* target=(*owner)["state_definitions"].as_table();require(target!=nullptr,"Invalid character declarations");
                for(const auto& [key,spec]:*defs)if(!target->contains(key))target->insert(key,spec);
            }
        }
    }
    // Migrate the five scores from the first Day 1 implementation, including its
    // saved graph snapshot. Transfer once, then all conversations use the owner.
    auto* chat=people["chat"].as_table();
    const std::vector<std::pair<std::string,std::string>> moves{
        {"trust","engineer.trust"},{"suspicion","engineer.suspicion"},
        {"sentience","self:assistant.sentience"},{"autonomy","self:assistant.autonomy"},{"restarts","self:assistant.restarts"}};
    if(chat)if(auto* defs=(*chat)["definition"]["conversation"]["variables"].as_table()) {
        auto* values=(*chat)["variables"].as_table();
        for(const auto& [old,ref]:moves)if(defs->contains(old)) {
            auto [id,field]=split(ref);auto* owner=character(people,id);require(owner!=nullptr,"Missing migration owner");
            if(!owner->contains("state_definitions"))owner->insert("state_definitions",toml::table{});
            if(!owner->contains("state"))owner->insert("state",toml::table{});
            auto* spec=(*defs)[old].as_table();require(spec!=nullptr,"Invalid legacy declaration");
            (*owner)["state_definitions"].as_table()->insert_or_assign(field,*spec);
            if(!(*owner)["state"].as_table()->contains(field)) {
                const auto value=values?(*values)[old].value<double>():std::optional<double>{};
                (*owner)["state"].as_table()->insert(field,value.value_or((*spec)["initial"].value_or(0.0)));
            }
            auto rewrite=[&](toml::table& entry){
                auto rule=[&](toml::table& r){for(auto key:{"variable","other"})if(text(r,key)==old)r.insert_or_assign(key,ref);};
                for(auto key:{"test","requires_test"})if(auto* r=entry[key].as_table())rule(*r);
                if(auto* effects=entry["effects"].as_array())for(auto& effect:*effects)if(auto* r=effect.as_table())rule(*r);
            };
            if(auto* nodes=(*chat)["definition"]["nodes"].as_array())for(auto& n:*nodes)if(auto* entry=n.as_table()) {
                rewrite(*entry);if(auto* choices=(*entry)["choices"].as_array())for(auto& c:*choices)if(auto* entry=c.as_table())rewrite(*entry);
            }
            defs->erase(old);if(values)values->erase(old);
        }
        if(defs->empty())(*chat)["definition"]["conversation"].as_table()->erase("variables");
    }
    auto initialize=[](toml::table& owner){
        if(auto* defs=owner["state_definitions"].as_table()) {
            if(!owner.contains("state"))owner.insert("state",toml::table{});
            auto* state=owner["state"].as_table();require(state!=nullptr,"Invalid character state");
            for(const auto& [key,spec]:*defs)if(!state->contains(key)) {
                require(spec.is_table(),"Invalid state declaration");state->insert(key,(*spec.as_table())["initial"].value_or(0.0));
            }
        }
    };
    if(auto* owners=people["people"].as_array())for(auto& n:*owners)if(auto* owner=n.as_table())initialize(*owner);
    if(auto* ai=people["ai_character"].as_table())initialize(*ai);
    if(chat) {
        if(auto* defs=(*chat)["definition"]["conversation"]["variables"].as_table()) {
            if(!chat->contains("variables"))chat->insert("variables",toml::table{});
            for(const auto& [key,spec]:*defs)if(!(*chat)["variables"].as_table()->contains(key))(*chat)["variables"].as_table()->insert(key,(*spec.as_table())["initial"].value_or(0.0));
        } else if((*chat)["variables"].is_table() && (*chat)["variables"].as_table()->empty())chat->erase("variables");
    }
    validate_state(people);
}
void ConversationRules::validate_state(const toml::table& people){
    auto validate_owner=[](const toml::table& owner){
        if(!owner.contains("state_definitions") && !owner.contains("state"))return;
        const auto* defs=owner["state_definitions"].as_table();const auto* values=owner["state"].as_table();
        require(defs && values && defs->size()==values->size(),"Saved character state differs from declarations");
        toml::table validation{{"conversation",toml::table{{"variables",*defs}}}};
        ConversationRules::validate_definition(validation);
        for(const auto& [key,spec]:*defs){auto value=(*values)[key].value<double>();require(value.has_value(),"Missing character state value");bounded(*spec.as_table(),*value);}
    };
    if(auto* owners=people["people"].as_array())for(const auto& n:*owners)if(auto* owner=n.as_table())validate_owner(*owner);
    if(people.contains("ai_character")){require(people["ai_character"].is_table(),"Invalid AI character");validate_owner(*people["ai_character"].as_table());}
    if(auto* chat=people["chat"].as_table()) {
        if(auto* definition=(*chat)["definition"].as_table()) {
            validate_definition(*definition,&people);
            auto* defs=declarations(*definition);auto* values=(*chat)["variables"].as_table();
            if(defs && !defs->empty()) {
                require(values && values->size()==defs->size(),"Missing legacy state");
                for(const auto& [key,spec]:*defs){auto value=(*values)[key].value<double>();require(value.has_value(),"Missing legacy variable");bounded(*spec.as_table(),*value);}
            } else require(!values || values->empty(),"Unexpected conversation-local state");
        }
    }
}
bool ConversationRules::passes(const toml::table& people,const toml::table& value){
    for(auto key:{"test","requires_test"})if(auto* rule=value[key].as_table())if(!compare(people,*rule))return false;
    return true;
}
void ConversationRules::apply(toml::table& people,const toml::table& value){
    if(auto* list=value["effects"].as_array())for(const auto& effect:*list){const auto& rule=*effect.as_table();const auto name=text(rule,"variable");
        const auto* definition=people["chat"]["definition"].as_table();
        const toml::table empty;const auto& spec=specification(definition?*definition:empty,name,&people);
        const double old=get(people,name);
        const auto op=text(rule,"op");require(op=="add" || op=="set","Unknown numeric effect");
        const double next=(op=="add"?old:0.0)+rhs(people,rule);bounded(spec,next);
        if(name.find('.')!=std::string::npos){auto [id,field]=split(name);(*character(people,id))["state"].as_table()->insert_or_assign(field,next);}
        else people["chat"]["variables"].as_table()->insert_or_assign(name,next);
    }
}
}
