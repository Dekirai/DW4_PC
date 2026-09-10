#include "action_event_tables_fixture.hpp"
#include "dw4/assets.hpp"

#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace action_event_fixture;
int checks{};
void check(bool condition,const char* reason) { ++checks; if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason,std::string_view expected={}) {
    bool caught=false;
    try { operation(); }
    catch (const ActionEventTableError& error) {
        caught=true;
        if (!expected.empty()) check(std::string_view(error.what()).find(expected)!=std::string_view::npos,
            "Malformed table did not reach the intended guard");
    }
    check(caught,reason);
}
void matching() {
    Fixture f; const auto se=f.sound_program(),starter=f.effect_program();
    check(se.source()=="synthetic/events.igb" && se.source_occurrence()==7 && se.reference()==f.se &&
        se.kind()==ActionEventConsumerKind::se && se.name()=="Synthetic sound consumer","Consumer retains explicit source, occurrence, type and info identity");
    check(se.rams()==std::vector<std::uint32_t>{f.ram_refs[0],f.ram_refs[1],f.ram_refs[0]} && starter.rams()==se.rams(),
        "Ordered RAM aliases and cross-consumer sharing are preserved");
    check(se.ram_owners()==se.rams(),"Ordinary RAM references retain their physical owner identity");
    check(!starter.name() && starter.kind()==ActionEventConsumerKind::action_starter,"Null consumer info name remains metadata");
    const auto sounds=match_action_event_tables(se,"p","RUN");
    check(sounds.size()==4 && sounds[0].model_index==2 && sounds[0].model_reference==f.se_rows[2],"Se selects the LAST matching model row");
    check(sounds[2].action_reference==f.sounds[2] && sounds[3].action_reference==f.sounds[2] && sounds[3].action_index==3,
        "Duplicate action references produce ordered distinct occurrences");
    check(!std::get<ActionEventSound>(sounds[0].payload).name && std::get<ActionEventSound>(sounds[1].payload).name=="" &&
        std::get<ActionEventSound>(sounds[2].payload).name=="foot_lp1","Null, empty and named sound payloads remain distinct");
    check(std::get<ActionEventSound>(sounds[0].payload).delay==-32768 && std::get<ActionEventSound>(sounds[1].payload).delay==32767 &&
        !sounds[0].has_rotation,"Sound delay sign-extends the serialized short without changing frame units");
    const auto effects=match_action_event_tables(starter,"p","RUN");
    check(effects.size()==4 && effects[0].model_index==0 && effects[0].model_reference==f.starter_rows[0],
        "Starter selects the FIRST matching model row");
    const auto& base=std::get<ModelEffectSubcommand>(effects[0].payload);
    const auto& data2=std::get<ModelEffectSubcommand>(effects[1].payload);
    const auto& derived=std::get<ModelEffectSubcommand>(effects[2].payload);
    check(base.model_name=="" && !base.sequence_name && data2.sequence_name=="" && derived.sequence_name=="SQ_ATCK",
        "Info-name null falls back to empty while nullable effect message payload is preserved");
    check(base.delay==16777216.0f && data2.delay==-1 && base.offset==std::array<float,3>{1,2,3} &&
        base.sequence_dead && !base.position_lock && data2.position_lock,"Effect payload retains signed float conversion, vectors and lifetime flags");
    check(base.rotation==std::array<float,3>{} && data2.rotation==std::array<float,3>{4,5,6} && derived.rotation==std::array<float,3>{} &&
        !effects[0].has_rotation && effects[1].has_rotation && !effects[2].has_rotation,
        "Only EXACT Data2 emits rotation; a subtype inheriting Data2 fields takes the base branch");
    const auto empty_sound=match_action_event_tables(se,"","");
    const auto empty_effect=match_action_event_tables(starter,"","");
    check(empty_sound.size()==1 && std::get<ActionEventSound>(empty_sound[0].payload).name=="empty key" && empty_effect.size()==1,
        "Null DATA model/action keys match incoming empty strings");
    check(match_action_event_tables(se,"P","RUN").empty() && match_action_event_tables(starter,"p","run").empty() &&
        match_action_event_tables(se,"missing","RUN").empty(),"Matching uses exact bytes and does not fall back across missing rows");
    auto changed=f; changed.contents(changed.se_last_actions,{}); changed.contents(changed.starter_first_actions,{});
    check(match_action_event_tables(changed.sound_program(),"p","RUN").empty() &&
        match_action_event_tables(changed.effect_program(),"p","RUN").empty(),"An empty selected model does not fall back to another duplicate model");
    auto repeated=f; repeated.contents(repeated.se_models,{repeated.se_rows[2],repeated.se_rows[2]});
    check(match_action_event_tables(repeated.sound_program(),"p","RUN")[0].model_index==1,"Repeated model object aliases preserve occurrence selection");
    const auto copy=starter;
    check(&copy.models()==&starter.models() && &copy.rams()==&starter.rams(),"Copies share immutable value-owned table data");
    auto detached=[] { Fixture local; return local.effect_program(); }();
    auto matches=match_action_event_tables(detached,"p","RUN");
    std::get<ModelEffectSubcommand>(matches[0].payload).offset[0]=99;
    check(std::get<ModelEffectSubcommand>(match_action_event_tables(detached,"p","RUN")[0].payload).offset[0]==1,
        "Matching returns independent payloads and does not borrow source document lifetime");
    const auto doc=f.document();
    const auto other=decode_action_event_tables(doc,f.se,se.source(),8);
    check(other.reference()==se.reference() && other.source_occurrence()!=se.source_occurrence(),"Repeated source paths retain explicit occurrence identity");
    auto aliases=doc;
    aliases.references[f.ram_refs[1]].value_index=aliases.references[f.ram_refs[0]].value_index;
    const auto aliased=decode_action_event_tables(aliases,f.se,se.source(),7);
    check(aliased.rams()==se.rams() && aliased.ram_owners()==std::vector<std::uint32_t>(3,f.ram_refs[0]),
        "Distinct serialized refs preserve their occurrences while sharing a resolved physical RAM owner");
}
void malformed() {
    Fixture f;
    const auto reject=[&](const Fixture& value,bool starter=false,std::string_view expected={}) {
        const auto doc=value.document();
        rejects([&]{decode_action_event_tables(doc,starter ? value.starter : value.se,"synthetic/events.igb",7);},
            "Malformed consumer table rejects",expected);
    };
    auto bad=f; bad.info_reference=null_ref; reject(bad,false,"not explicitly exported");
    bad=f; bad.contents(bad.info_reference,{bad.starter}); reject(bad,false,"not explicitly exported");
    bad=f; bad.contents(bad.info_reference,{bad.se,null_ref}); reject(bad,false,"null object row");
    bad=f; bad.records[bad.se].type=derived_se_info; reject(bad,false,"exact exported");
    bad=f; bad.scalar(bad.se,5,null_ref); reject(bad,false,"list is null");
    bad=f; bad.contents(bad.rams,{bad.se}); reject(bad,false,"object type");
    bad=f; bad.scalar(bad.ram_refs[0],5,2); reject(bad,false,"boolean");
    bad=f; bad.scalar(bad.starter,5,bad.se_models); reject(bad,true,"base data");
    bad=f; bad.scalar(bad.se_models,3,4097); reject(bad,false,"list budget");
    bad=f; bad.scalar(bad.se_models,3,0); reject(bad,false,"list budget");
    bad=f; bad.contents(bad.se_last_actions,{null_ref}); reject(bad,false,"null object row");
    bad=f; bad.scalar(bad.effects[0],6,2); reject(bad,true,"boolean");
    bad=f; bad.records[bad.effects[0]].values[5]=floats({1,std::numeric_limits<float>::infinity(),0}); reject(bad,true,"non-finite");
    bad=f; bad.records[bad.effects[1]].values[8]=floats({1,0,std::numeric_limits<float>::quiet_NaN()}); reject(bad,true,"non-finite");
    bad=f; bad.records[bad.effects[2]].values[8]=floats({1,0,std::numeric_limits<float>::quiet_NaN()});
    check(!match_action_event_tables(bad.effect_program(),"p","RUN")[2].has_rotation,"Unused inherited rotation is not spuriously read by the base branch");
    bad=f; bad.text(bad.sounds[0],3,std::string(8192,'\0')); reject(bad,false,"Serialized");
    bad=f; bad.contents(bad.se_models,std::vector<std::uint32_t>(4096,bad.se_rows[2]));
    bad.contents(bad.se_last_actions,std::vector<std::uint32_t>(32,bad.sounds[2])); reject(bad,false,"aggregate action budget");
    const auto doc=f.document();
    for (const auto [ref,slot]:std::array<std::pair<std::uint32_t,std::uint16_t>,4>{
        std::pair{f.se_rows[0],std::uint16_t{3}},std::pair{f.ram_refs[0],std::uint16_t{2}},
        std::pair{f.sounds[0],std::uint16_t{4}},std::pair{f.effects[1],std::uint16_t{5}}}) {
        auto typed=doc; auto& object=typed.objects[typed.references[ref].value_index];
        const_cast<IgbFieldValue*>(typed.field(object,slot))->type_index=2;
        rejects([&]{decode_action_event_tables(typed,ref==f.effects[1] ? f.starter : f.se,"synthetic/events.igb",7);},
            "Inherited field metadata cannot silently reinterpret payload bytes","field type or width");
    }
    auto short_width=doc; auto& sound=short_width.objects[short_width.references[f.sounds[0]].value_index];
    const_cast<IgbFieldValue*>(short_width.field(sound,4))->size=4;
    rejects([&]{decode_action_event_tables(short_width,f.se,"synthetic/events.igb",7);},"Signed short width is mandatory","field type or width");
    rejects([&]{decode_action_event_tables(doc,f.se,"",7);},"Source is required");
    rejects([&]{decode_action_event_tables(doc,f.se,std::string("a\0b",3),7);},"Source embedded NUL rejects");
    rejects([&]{decode_action_event_tables(doc,f.se,"synthetic/events.igb",null_ref);},"Source occurrence sentinel rejects");
    rejects([&]{match_action_event_tables(f.sound_program(),std::string("p\0x",3),"RUN");},"Incoming keys cannot contain hidden suffix bytes");
    rejects([&]{match_action_event_tables(f.sound_program(),"p",std::string(4097,'a'));},"Incoming event name bytes are bounded");
}
void original(const std::filesystem::path& root) {
    const auto archive=assets::AfsArchive::open(root/"files"/"ndmw.afs");
    const auto sound_doc=read_igb_document(archive.read(178));
    const auto se=decode_action_event_tables(sound_doc,232,"ndmw.afs/178",178);
    std::size_t sound_rows=0; for (const auto& row:se.models()) sound_rows+=row.actions.size();
    check(se.models().size()==78 && sound_rows==2173 && se.name()=="beAsmSESet","Original preliminary Se export table dimensions match");
    const auto running=match_action_event_tables(se,"p_agumon","RUNNS1");
    check(running.size()==1 && running[0].model_reference==163 && running[0].action_reference==465 &&
        std::get<ActionEventSound>(running[0].payload).name=="foot_lp1" && std::get<ActionEventSound>(running[0].payload).delay==6,
        "Original Agumon RUNNS1 translates exactly to the real delayed sound payload");
    const auto starter_doc=read_igb_document(archive.read(179));
    std::size_t exports=0,models=0,actions=0,data2=0;
    for (const auto ref:starter_doc.object_list(*starter_doc.info_list_reference)) if (ref &&
        starter_doc.metadata.object_types[starter_doc.object(*ref).type_index].name=="beActionStarterInfo") {
        const auto starter=decode_action_event_tables(starter_doc,*ref,"ndmw.afs/179",179);
        ++exports; models+=starter.models().size();
        for (const auto& model:starter.models()) for (const auto& action:model.actions) {
            ++actions;
            if (action.type=="beActionStarterData2") ++data2;
            const auto& effect=std::get<ModelEffectSubcommand>(action.payload);
            check(effect.model_name==starter.name().value_or(""),"Every original effect targets its actual consumer info name");
        }
        if (*ref==1646) {
            const auto matches=match_action_event_tables(starter,"e_nume","BLST01");
            check(matches.size()==1 && matches[0].model_reference==1633 && matches[0].action_reference==1632 && !matches[0].has_rotation,
                "Original first Starter export follows actual archive model/action references");
            const auto& effect=std::get<ModelEffectSubcommand>(matches[0].payload);
            check(effect.sequence_name=="SQ_ATCK" && effect.delay==47 && effect.offset==std::array<float,3>{0,7,6} &&
                effect.rotation==std::array<float,3>{} && !effect.sequence_dead && !effect.position_lock,
                "Original Starter base row has its exact signed delay, offset and flags");
        }
    }
    check(exports==58 && models==958 && actions==5887 && data2>0,"All entry179 original Starter exports and Data2 rows decode");
    std::cout<<"Original tables: Se models=78 actions=2173; Starter exports="<<exports<<" models="<<models<<" actions="<<actions<<" Data2="<<data2<<'\n';
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        matching(); malformed();
        if (args.size()==2) original(args[1]); else if (args.size()!=1) throw std::runtime_error("Usage: dw4_action_event_tables_tests [EXTRACTED_ROOT]");
        std::cout<<checks<<" action event table checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Action event table failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
