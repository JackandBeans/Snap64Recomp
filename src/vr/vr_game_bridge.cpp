// Guest offsets: matching 3a236dc decomp symbols, sys/om.h, world/world.h.
#include "vr_service.h"
#include "vr_messages.h"
#include "recomp.h"
#include <cstring>
#include <cstdlib>
#include "paths.h"
#include <json/json.hpp>
#include <fstream>
#include <map>
#include <chrono>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "funcs.h"
}
namespace snap {extern std::atomic<uint32_t> g_scene_overlay_rom;}
namespace {
using namespace snap::vr;
constexpr uint32_t mainCamera=0x80382C30, movement=0x80366BA4, paused=0x80382D20;
bool pointer(uint32_t a){return a>=0x80000000&&a<0x807fff00;}
uint32_t word(uint8_t* rdram,uint32_t a){return MEM_W(0,(int32_t)a);}
float scalar(uint8_t* rdram,uint32_t a){uint32_t u=word(rdram,a);float f;std::memcpy(&f,&u,4);return f;}
void scalar(uint8_t* rdram,uint32_t a,float f){uint32_t u;std::memcpy(&u,&f,4);MEM_W(0,(int32_t)a)=u;}
Vec3 vector(uint8_t* rdram,uint32_t a){return {scalar(rdram,a),scalar(rdram,a+4),scalar(rdram,a+8)};}
void vector(uint8_t* rdram,uint32_t a,Vec3 v){scalar(rdram,a,v.x);scalar(rdram,a+4,v.y);scalar(rdram,a+8,v.z);}
struct PhotoLens {float fov=30;Vec3 up{0,1,0};};
std::array<PhotoLens,2> detectorLens;
std::map<std::string,PhotoLens> photoLenses;
std::string photoKey(uint8_t* rdram,uint32_t photo) {
    // Pose + original capture time survive album/report copies and score flags.
    std::string key;const char* hex="0123456789abcdef";
    for(uint32_t i=4;i<32;i++){auto b=uint8_t(MEM_BU(i,(int32_t)photo));key+=hex[b>>4];key+=hex[b&15];}return key;
}
void loadPhotoLenses() {
    static bool loaded=false;if(loaded)return;loaded=true;
    try {
        std::ifstream file(snap::base_dir()/"saves/vr-photo-lenses.json");if(!file)return;nlohmann::json j;file>>j;
        for(auto it=j.begin();it!=j.end();++it){auto& v=it.value();float f=v.at("fov");Vec3 up{v.at("up")[0],v.at("up")[1],v.at("up")[2]};if(std::isfinite(f)&&f>=20&&f<=60&&length(up)>.9f&&length(up)<1.1f)photoLenses[it.key()]={f,up};}
    }catch(const std::exception& e){fprintf(stderr,"[SNAP-VR] Photo lens metadata could not be loaded: %s\n",e.what());}
}
void savePhotoLenses() {
    try {
        nlohmann::json j=nlohmann::json::object();for(const auto& [key,lens]:photoLenses)j[key]={{"fov",lens.fov},{"up",{lens.up.x,lens.up.y,lens.up.z}}};
        auto path=snap::base_dir()/"saves/vr-photo-lenses.json";auto temp=path;temp+=".tmp";std::filesystem::create_directories(path.parent_path());
        {std::ofstream file(temp);file<<j.dump(2)<<'\n';file.flush();if(!file)throw std::runtime_error("write failed");}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("replace failed");
#else
        std::filesystem::rename(temp,path);
#endif
    }catch(const std::exception& e){fprintf(stderr,"[SNAP-VR] Photo lens metadata could not be saved: %s\n",e.what());}
}
void lens(uint8_t* rdram) {
    if(!requested.load())return;
    auto& s=shared();std::lock_guard lock(s.mutex);
    if(!s.game.course||s.game.cinematic||s.interaction.epoch!=s.game.epoch)return;
    uint32_t cam=word(rdram,mainCamera);if(!pointer(cam))return;
    const auto p=compose(Pose{yaw(s.game.cartYaw+pi),s.game.cartPosition},s.interaction.lensInCart);Vec3 forward=rotate(p.orientation,{0,0,-1});
    vector(rdram,cam+0x3c,p.position);vector(rdram,cam+0x48,p.position+forward*100);
    Vec3 up=rotate(p.orientation,{0,1,0});vector(rdram,cam+0x54,up);
    // Use the full up vector in both the N64 render and photo-frustum paths.
    uint32_t mtx=word(rdram,cam+0x68);if(pointer(mtx))MEM_B(4,(int32_t)mtx)=12; // LOOKAT_REFLECT
    scalar(rdram,cam+0x20,s.interaction.fovY);
}
thread_local bool releasing=false;
thread_local bool tutorialBlinkReset=false;
thread_local Vec3 releaseVelocity,releasePosition;
struct Impact {uint64_t epoch,born;};
std::map<uint32_t,Impact> impacts;
void cinematic(bool active) {
    if(!requested.load())return;
    auto& s=shared();std::lock_guard lock(s.mutex);s.game.cinematic=active;s.releases.clear();
}
}
extern "C" void PlayerModel_Init(uint8_t* rdram,recomp_context* ctx){cinematic(true);__real_PlayerModel_Init(rdram,ctx);}
extern "C" void Camera_StartStopCutscene(uint8_t* rdram,recomp_context* ctx){cinematic(true);__real_Camera_StartStopCutscene(rdram,ctx);}
extern "C" void func_803571C4_4F75D4(uint8_t* rdram,recomp_context* ctx){cinematic(true);__real_func_803571C4_4F75D4(rdram,ctx);}
extern "C" void func_803572B0_4F76C0(uint8_t* rdram,recomp_context* ctx){__real_func_803572B0_4F76C0(rdram,ctx);cinematic(false);}
extern "C" void Items_RemovePesterBall(uint8_t* rdram,recomp_context* ctx) {
    if(requested.load()) {
        auto& s=shared();std::lock_guard lock(s.mutex);auto obj=uint32_t(ctx->r4);
        if(!impacts.count(obj))impacts[obj]={s.game.epoch,s.game.frame};
    }
    __real_Items_RemovePesterBall(rdram,ctx);
}
extern "C" void mainCameraRender(uint8_t* rdram,recomp_context* ctx) {
    if(snap::vr::requested.load()) {
        auto& s=snap::vr::shared();std::lock_guard lock(s.mutex);
        auto& g=s.game;g.course=true;++g.frame;
        g.cartPosition=vector(rdram,movement+0xc);g.cartYaw=scalar(rdram,movement+0x1c);
        g.paused=MEM_BU(0,(int32_t)paused)!=0;
        g.cartVelocity=vector(rdram,0x80382CA0)*30;
        g.apples=word(rdram,0x803AE51C)&1;g.pesterBalls=word(rdram,0x803AE51C)&2;
        g.film=std::clamp(60-int(word(rdram,0x800AC0E0)),0,60);
        g.itemReady=word(rdram,0x80382CB4)==0&&!word(rdram,0x80382D0C);
        g.smoke.clear();
        for(auto it=impacts.begin();it!=impacts.end();) {
            if(it->second.epoch!=g.epoch){it=impacts.erase(it);continue;}
            if(g.frame-it->second.born>36){++it;continue;}
            uint32_t root=pointer(it->first)?word(rdram,it->first+0x48):0;
            if(pointer(root))g.smoke.push_back({vector(rdram,root+0x1c),it->second.born});
            ++it;
        }
    }
    lens(rdram);
    if(snap::vr::preview&&std::getenv("SNAP_VR_PROJECTILE_TEST")) {
        static unsigned frames=0;
        if(++frames%30==1) {
            auto& s=shared();GameState g;{std::lock_guard lock(s.mutex);g=s.game;}
            recomp_context call=*ctx;call.r29-=0x80;call.r4=call.r29+0x20;call.r5=call.r29+0x30;
            vector(rdram,uint32_t(call.r5),{});
            const auto rotation=yaw(g.cartYaw+pi);
            releasing=true;releaseVelocity={};
            releasePosition=g.cartPosition+rotate(rotation,{-20,180,-100});
            Items_SpawnApple(rdram,&call);
            call=*ctx;call.r29-=0x80;call.r4=call.r29+0x20;call.r5=call.r29+0x30;
            vector(rdram,uint32_t(call.r5),{});
            releasePosition=g.cartPosition+rotate(rotation,{90,180,-160});
            Items_SpawnPesterBall(rdram,&call);
            releasing=false;
        }
    }
    __real_mainCameraRender(rdram,ctx);
}
extern "C" void Msg_ShowMessage(uint8_t* rdram,recomp_context* ctx) {
    if(snap::vr::requested.load()&&pointer(uint32_t(ctx->r4))) {
        std::string source;
        for(unsigned i=0;i<1024;i++){char c=MEM_BU(i,(int32_t)ctx->r4);if(!c)break;source+=c;}
        auto text=snap::vr::messageText(source);
        auto& s=shared();std::lock_guard lock(s.mutex);
        if(std::getenv("SNAP_VR_DIAG")&&s.game.message!=text)fprintf(stderr,"[SNAP-VR-MESSAGE] %08x [%s]\n",unsigned(ctx->r4),text.c_str());
        s.game.message=std::move(text);
        s.game.messageContinue=s.game.message.find("Try to take")!=std::string::npos||s.game.message.find("LOOK AROUND")!=std::string::npos||s.game.message.find("out of film")!=std::string::npos;
    }
    __real_Msg_ShowMessage(rdram,ctx);
}
extern "C" void Msg_Reset(uint8_t* rdram,recomp_context* ctx) {
    __real_Msg_Reset(rdram,ctx);
    if(snap::vr::requested.load()&&!tutorialBlinkReset){auto& s=shared();std::lock_guard lock(s.mutex);s.game.message.clear();s.game.messageContinue=false;}
}
extern "C" void Tutorial_ShowMessage(uint8_t* rdram,recomp_context* ctx) {
    const unsigned id=unsigned(ctx->r4);
    const bool active=requested.load()&&MEM_B(0,(int32_t)0x803AE516)!=1;
    tutorialBlinkReset=active&&id==0;
    __real_Tutorial_ShowMessage(rdram,ctx);
    tutorialBlinkReset=false;
    if(!active||id==0||id>4)return;
    static const char* instructions[]={"",
        "GRIP THE CAMERA IN THE HOLSTER TO AIM. KEEP HOLDING GRIP.",
        "AIM THE CAMERA AT THE POKEMON. PULL THE HOLDING HAND'S TRIGGER TO SHOOT.",
        "TRY TO TAKE LOTS OF POKEMON PICTURES! AIM WITH YOUR HANDHELD CAMERA.",
        "TURN YOUR HEAD TO LOOK AROUND. MOVE AND TURN THE CAMERA TO AIM."};
    auto& s=shared();std::lock_guard lock(s.mutex);
    s.game.message=instructions[id];s.game.messageContinue=id>=3;
}
// Photo list filtering remains the game's original lens-frustum decision.
// Add only the otherwise skipped draw, without allocating a detector region.
#define VR_POKEMON_RENDER(name,model) \
extern "C" void name(uint8_t* rdram,recomp_context* ctx) { \
    if(snap::vr::requested.load()) { \
        recomp_context test=*ctx;Pokemon_GetFlag100(rdram,&test); \
        if(test.r2){model(rdram,ctx);return;} \
    } \
    __real_##name(rdram,ctx); \
}
VR_POKEMON_RENDER(renderPokemonModelTypeIFogged,renderModelTypeIFogged)
VR_POKEMON_RENDER(renderPokemonModelTypeJFogged,renderModelTypeJFogged)
VR_POKEMON_RENDER(renderPokemonModelTypeBFogged,renderModelTypeBFogged)
VR_POKEMON_RENDER(renderPokemonModelTypeDFogged,renderModelTypeDFogged)
VR_POKEMON_RENDER(renderPokemonModelTypeI,renRenderModelTypeI)
VR_POKEMON_RENDER(renderPokemonModelTypeB,renRenderModelTypeB)
VR_POKEMON_RENDER(renderPokemonModelTypeD,renRenderModelTypeD)
extern "C" void PokemonDetector_InitDetector(uint8_t* rdram,recomp_context* ctx) {
    __real_PokemonDetector_InitDetector(rdram,ctx);
    if(snap::vr::requested.load()) {
        auto cam=word(rdram,mainCamera);unsigned id=MEM_HU(0,(int32_t)0x803AEF34)&1;
        if(pointer(cam))detectorLens[id]={scalar(rdram,cam+0x20),vector(rdram,cam+0x54)};
        auto& state=shared();std::lock_guard lock(state.mutex);state.detectorFrames[id]=state.game.frame;
    }
}
extern "C" void makePhoto(uint8_t* rdram,recomp_context* ctx) {
    auto index=word(rdram,0x800AC0E0);auto context=word(rdram,0x803AEF30)&1;
    __real_makePhoto(rdram,ctx);
    if(snap::vr::requested.load()&&index<60&&word(rdram,0x800AC0E0)>index) {
        loadPhotoLenses();photoLenses[photoKey(rdram,0x800B0598+index*0x3a0)]=detectorLens[context];savePhotoLenses();
    }
}
extern "C" void func_8009D8A8(uint8_t* rdram,recomp_context* ctx) {
    auto cam=uint32_t(ctx->r4),photo=uint32_t(ctx->r5);__real_func_8009D8A8(rdram,ctx);
    if(!pointer(cam)||!pointer(photo))return;loadPhotoLenses();auto it=photoLenses.find(photoKey(rdram,photo));
    if(it!=photoLenses.end()){scalar(rdram,cam+0x20,it->second.fov);vector(rdram,cam+0x54,it->second.up);}
}
extern "C" void updateCameraZoomedIn(uint8_t* rdram,recomp_context* ctx) {
    auto obj=ctx->r4;__real_updateCameraZoomedIn(rdram,ctx);lens(rdram);
    if(snap::vr::requested.load()) {
        // The original zoom mode suspends its item process. Run its cooldown,
        // flute, and VR release bridge, suppressing A/B's legacy throws.
        uint16_t pressed=MEM_HU(0,(int32_t)0x80049752);
        MEM_H(0,(int32_t)0x80049752)=pressed&~0xc000;
        struct Guard {uint8_t* rdram;uint16_t buttons;~Guard(){MEM_H(0,(int32_t)0x80049752)=buttons;}}guard{rdram,pressed};
        recomp_context call=*ctx;call.r4=obj;handleItemButtonsPress(rdram,&call);
    }
}
extern "C" void updateCameraZoomedOut(uint8_t* rdram,recomp_context* ctx) {__real_updateCameraZoomedOut(rdram,ctx);lens(rdram);}
extern "C" void Items_InitItem(uint8_t* rdram,recomp_context* ctx) {
    uint32_t obj=uint32_t(ctx->r4);
    impacts.erase(obj);
    if(releasing) {
        recomp_context call=*ctx;call.r29-=0x60;call.r5=call.r29+0x20;
        vector(rdram,uint32_t(call.r5),releasePosition);__real_Items_InitItem(rdram,&call);
    }else __real_Items_InitItem(rdram,ctx);
    if(releasing&&pointer(obj)){uint32_t item=word(rdram,obj+0x58);if(pointer(item))vector(rdram,item+8,releaseVelocity);}
}
extern "C" void renderModelTypeDFogged(uint8_t* rdram,recomp_context* ctx) {
    // Opening sky mesh (payload 8037ED08, cloud texture 8035A2A8),
    // including its eight culling vertices. Expand about its local bounds
    // center once per segment load. This changes no terrain or actor vertices.
    if(requested.load() && snap::g_scene_overlay_rom.load()==0xA08E30 &&
       MEM_H(0,(int32_t)0x8036AA78)==0 && MEM_H(2,(int32_t)0x8036AA78)==-892 &&
       MEM_H(4,(int32_t)0x8036AA78)==1711) {
        for(auto [base,count]:{std::pair<uint32_t,unsigned>{0x8036AA78,24},{0x803717A8,8}})
            for(unsigned i=0;i<count;i++) {
                int32_t vertex=int32_t(base+i*16);
                MEM_H(0,vertex)=int16_t(MEM_H(0,vertex)*4-2655);
                MEM_H(2,vertex)=int16_t(MEM_H(2,vertex)*4+27);
                MEM_H(4,vertex)=int16_t(MEM_H(4,vertex)*4-556);
            }
    }
    __real_renderModelTypeDFogged(rdram,ctx);
}
extern "C" void renderModelTypeBFogged(uint8_t* rdram,recomp_context* ctx) {
    // Match the original item tree's display list, including trees recreated
    // from PhotoData. Do not identify items by reusable GObj IDs or userData.
    const uint32_t obj=uint32_t(ctx->r4);
    const uint32_t root=pointer(obj)?word(rdram,obj+0x48):0;
    const uint32_t child=pointer(root)?word(rdram,root+0x10):0;
    int kind=-1;
    if(requested.load()&&pointer(child)) {
        const uint32_t world=uint32_t(section_addresses[12]);
        const uint32_t payload=word(rdram,child+0x50);
        if(pointer(world)&&payload) {
            if(payload==word(rdram,world+0x9630+48))kind=0; // apple tree child
            else if(payload==word(rdram,world+0x7898+48))kind=1; // Pester Ball
        }
    }
    static std::array<uint32_t,2> lists{};
    static bool attempted=false;
    if(kind>=0&&!attempted) {
        attempted=true;
        try {
            std::ifstream file(snap::base_dir()/"assets/vr/projectiles.bin",std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
            auto read=[&](size_t i){return uint32_t(bytes.at(i))<<24|uint32_t(bytes.at(i+1))<<16|uint32_t(bytes.at(i+2))<<8|bytes.at(i+3);};
            constexpr uint32_t base=0x80F00000;
            if(bytes.size()<16||bytes.size()>0x100000||read(0)!=0x56524931||read(4)!=bytes.size())throw std::runtime_error("Invalid projectile asset header");
            for(int i=0;i<2;i++){auto address=read(8+i*4);if(address<base+16||address>=base+bytes.size()||(address&7))throw std::runtime_error("Invalid projectile display list");}
            for(size_t i=0;i<bytes.size();i++)MEM_B(i,(int32_t)base)=bytes[i];
            lists={read(8),read(12)};
            fprintf(stderr,"[SNAP-VR] Replacement apple/Pester Ball world meshes loaded (%zu bytes)\n",bytes.size());
        }catch(const std::exception& e){fprintf(stderr,"[SNAP-VR] Projectile model replacement unavailable: %s\n",e.what());}
    }
    if(kind<0||!lists[kind]){__real_renderModelTypeBFogged(rdram,ctx);return;}
    // Attach to the root to avoid the original sprite child's billboard
    // transform. Retain root movement, bounce, shrink and photo transforms.
    const uint32_t payload=word(rdram,root+0x50),materials=word(rdram,root+0x80);
    struct RestoreItem {
        uint8_t* rdram;uint32_t root,child,payload,materials;
        ~RestoreItem(){MEM_W(0,(int32_t)(root+0x10))=child;MEM_W(0,(int32_t)(root+0x50))=payload;MEM_W(0,(int32_t)(root+0x80))=materials;}
    }restore{rdram,root,child,payload,materials};
    MEM_W(0,(int32_t)(root+0x10))=0;MEM_W(0,(int32_t)(root+0x50))=lists[kind];MEM_W(0,(int32_t)(root+0x80))=0;
    __real_renderModelTypeBFogged(rdram,ctx);
}
extern "C" void handleItemButtonsPress(uint8_t* rdram,recomp_context* ctx) {
    if(!snap::vr::requested.load()){__real_handleItemButtonsPress(rdram,ctx);return;}
    auto& state=snap::vr::shared();snap::vr::Throw item{};bool release=false;
    {
        std::lock_guard lock(state.mutex);
        if(word(rdram,0x80382D0C)||MEM_BU(0,(int32_t)paused)||word(rdram,0x80382CB4))state.releases.clear();
        while(!state.releases.empty()&&state.releases.front().epoch!=state.game.epoch)state.releases.pop_front();
        if(!state.releases.empty()) {
            item=state.releases.front();state.releases.pop_front();
            release=(item.item==Held::Apple&&state.game.apples)||(item.item==Held::PesterBall&&state.game.pesterBalls);
        }
    }
    uint16_t pressed=MEM_HU(0,(int32_t)0x80049752);
    uint16_t buttons=pressed&~0xc000;
    if(release) {
        // Use the original input branch for sounds, flute cancellation, icons,
        // cooldown and projectile creation. Only its spawn pose/velocity differ.
        buttons&=~0x4;buttons|=item.item==Held::Apple?0x8000:0x4000;
        releasePosition=item.position;releaseVelocity=item.velocity*(1.0f/30);
    }
    releasing=release;MEM_H(0,(int32_t)0x80049752)=buttons;
    struct Guard{uint8_t* rdram;uint16_t pressed;~Guard(){releasing=false;MEM_H(0,(int32_t)0x80049752)=pressed;}}guard{rdram,pressed};
    __real_handleItemButtonsPress(rdram,ctx);
}

static void nameInput(uint8_t* rdram,recomp_context* ctx) {
    if(!snap::vr::requested.load()||snap::g_scene_overlay_rom.load()!=0xA5CC50u||MEM_BU(0,(int32_t)0x80168144)!=1)return;
    auto input=uint32_t(ctx->r2);if(!pointer(input))return;
    uint32_t buttons=word(rdram,input+0x18);NameCell cell;
    {auto& state=shared();std::lock_guard lock(state.mutex);
        if(!state.tracking.focused||!state.tracking.headValid)return;
        cell=(buttons&0x8000)&&state.nameClick.valid()?state.nameClick:state.namePointer;
        if(buttons&0x8000)state.nameClick={};
    }
    if(!cell.valid())return;
    if(std::getenv("SNAP_VR_NAME_TEST")&&(buttons&(0x8000|0x4000)))
        fprintf(stderr,"[SNAP-VR-NAME-TEST] cell %d,%d buttons %04X previous name bytes %u\n",cell.x,cell.y,buttons,MEM_BU(0,(int32_t)0x80168143));
    // No synthesized stick movement: the pointed key and the confirmed key
    // agree even when trigger and a new hover arrive in the same game tick.
    MEM_W(0,(int32_t)0x80168120)=cell.x;
    MEM_W(0,(int32_t)0x80168124)=cell.y;
    if(cell.y<19)MEM_W(0,(int32_t)0x80168128)=cell.x;
    MEM_W(0,(int32_t)(input+0x18))=buttons&~0xf0000u;
    recomp_context call=*ctx;func_800E2200_A5D5B0(rdram,&call);
    call=*ctx;call.r4=(int32_t)0x80168140;func_800E2A84_A5DE34(rdram,&call);
}

namespace {
MenuRect menuFocus;
bool menuFocusShown=false;
uint64_t menuEpoch=0,seenClick=0;
MenuPoint pendingClick;
using Clock=std::chrono::steady_clock;
Clock::time_point nextStep{},clickDeadline{},frozenUntil{};
struct PromptButton {MenuRect rect;unsigned button;uint64_t epoch;bool shown=true;};
std::vector<PromptButton> promptButtons;
uint64_t currentEpoch(){auto& s=shared();std::lock_guard lock(s.mutex);return s.game.epoch;}
unsigned promptHit(MenuPoint point,uint64_t epoch) {
    for(const auto& b:promptButtons)if(b.shown&&b.epoch==epoch) {
        MenuRect rect=b.rect;rect.w=std::min(95.f,310.f-rect.x);
        for(const auto& next:promptButtons)if(next.shown&&next.epoch==epoch&&std::abs(next.rect.y-rect.y)<4&&next.rect.x>rect.x)rect.w=std::min(rect.w,next.rect.x-rect.x-4);
        if(rect.contains(point))return b.button;
    }
    return 0;
}
void menuInput(uint8_t* rdram,recomp_context* ctx) {
    constexpr uint32_t mailbox=0x80C00F00;
    MEM_W(0,(int32_t)mailbox)=0;
    if(!snap::vr::requested.load())return;
    if(snap::g_scene_overlay_rom.load()==0xA08E30u) {
        auto& state=shared();std::lock_guard lock(state.mutex);
        state.game.cinematic=MEM_BU(0,(int32_t)0x800E832B)==5;
    }
    MenuPoint point,click;uint64_t serial,epoch;bool active;
    {auto& s=shared();std::lock_guard lock(s.mutex);
        point=s.menuPointer;click=s.menuClick;serial=s.menuClickSerial;epoch=s.game.epoch;
        active=s.tracking.focused&&s.tracking.headValid&&(!s.game.course||s.game.paused);
    }
    if(epoch!=menuEpoch){menuEpoch=epoch;pendingClick={};seenClick=serial;menuFocusShown=false;}
    if(!active){pendingClick={};return;}
    uint32_t input=uint32_t(ctx->r2);if(!pointer(input))return;
    auto buttons=word(rdram,input+0x18);auto now=Clock::now();
    if(!point.valid||(buttons&(0x4000|0x1000)))pendingClick={};
    // Trigger coordinates remain fixed until the original menu reaches them.
    if(serial!=seenClick&&(buttons&0x8000)){seenClick=serial;pendingClick=click;clickDeadline=now+std::chrono::milliseconds(1500);}
    if(now>clickDeadline)pendingClick={};
    MenuPoint target=pendingClick.valid?pendingClick:point;
    if(target.valid){MEM_W(0,(int32_t)mailbox)=0x56525054;scalar(rdram,mailbox+4,target.x);scalar(rdram,mailbox+8,target.y);}
    bool name=snap::g_scene_overlay_rom.load()==0xA5CC50u;
    // Native patched pages and name entry perform exact hit tests themselves.
    bool pages=MEM_BU(0,(int32_t)0x80C0003D)!=0;
    unsigned prompt=promptHit(target,epoch);
    if(prompt&&(buttons&0x8000)) {
        buttons=(buttons&~0x8000u)|prompt;pendingClick={};
        MEM_H(0,(int32_t)0x80049752)=(MEM_HU(0,(int32_t)0x80049752)&~0x8000u)|prompt;
        MEM_W(0,(int32_t)(input+0x18))=buttons;
    }else if(menuFocusShown&&!name&&!pages&&target.valid&&now>=frozenUntil&&!prompt) {
        unsigned direction=menuFocus.direction(target);
        if(pendingClick.valid)buttons&=~0x8000u;
        if(menuFocus.contains(target)) {
            if(pendingClick.valid){buttons|=0x8000;pendingClick={};}
        }else if(direction&&now>=nextStep){buttons=(buttons&~0xf0000u)|direction;nextStep=now+std::chrono::milliseconds(90);}
        MEM_W(0,(int32_t)(input+0x18))=buttons;
    }else pendingClick={};
}
}
#define VR_FOCUS_HOOK(name,change) extern "C" void name(uint8_t* rdram,recomp_context* ctx){{auto& s=shared();std::lock_guard lock(s.mutex);if(menuEpoch!=s.game.epoch){menuEpoch=s.game.epoch;pendingClick={};menuFocusShown=false;}}change;__real_##name(rdram,ctx);}
VR_FOCUS_HOOK(FocusMark_SetTargetPos, menuFocus.x=int32_t(ctx->r4);menuFocus.y=int32_t(ctx->r5))
VR_FOCUS_HOOK(FocusMark_SetPos, menuFocus.x=int32_t(ctx->r4);menuFocus.y=int32_t(ctx->r5))
VR_FOCUS_HOOK(FocusMark_SetTargetSize, menuFocus.w=int32_t(ctx->r4);menuFocus.h=int32_t(ctx->r5))
VR_FOCUS_HOOK(FocusMark_SetSize, menuFocus.w=int32_t(ctx->r4);menuFocus.h=int32_t(ctx->r5))
VR_FOCUS_HOOK(FocusMark_Show, menuFocusShown=ctx->r4!=0)
VR_FOCUS_HOOK(FocusMark_Create, menuFocus=(MenuRect{22,29,62,13});menuFocusShown=true)
extern "C" void func_800AA38C(uint8_t* rdram,recomp_context* ctx){__real_func_800AA38C(rdram,ctx);menuInput(rdram,ctx);nameInput(rdram,ctx);}
extern "C" void func_800AA740(uint8_t* rdram,recomp_context* ctx){
    // This function intentionally returns NEUTRAL input to freeze a menu.
    // Injecting navigation here bypasses the game's confirmation modal.
    frozenUntil=Clock::now()+std::chrono::milliseconds(100);
    __real_func_800AA740(rdram,ctx);
}
extern "C" void UIButtonImage_Create(uint8_t* rdram,recomp_context* ctx) {
    int x=int32_t(ctx->r4),y=int32_t(ctx->r5),code=int32_t(ctx->r6)&255,size=int32_t(ctx->r7);
    __real_UIButtonImage_Create(rdram,ctx);
    unsigned button=code=='a'?0x8000:code=='b'?0x4000:code=='s'?0x1000:0;
    if(ctx->r2&&button){auto epoch=currentEpoch();std::erase_if(promptButtons,[&](const auto& b){return b.epoch!=epoch||(b.rect.x==x&&b.rect.y==y);});promptButtons.push_back({{float(x),float(y),float(size),float(size)},button,epoch});}
}
extern "C" void UIButtonImage_DeleteAll(uint8_t* rdram,recomp_context* ctx){promptButtons.clear();__real_UIButtonImage_DeleteAll(rdram,ctx);}
extern "C" void UIButtonImage_DeleteInRect(uint8_t* rdram,recomp_context* ctx){
    int x=int32_t(ctx->r4),y=int32_t(ctx->r5),right=int32_t(ctx->r6),bottom=int32_t(ctx->r7);
    std::erase_if(promptButtons,[&](const auto& b){return b.rect.x>=x&&b.rect.x<=right&&b.rect.y>=y&&b.rect.y<=bottom;});
    __real_UIButtonImage_DeleteInRect(rdram,ctx);
}
extern "C" void UIButtonImage_SetState(uint8_t* rdram,recomp_context* ctx){for(auto& b:promptButtons)b.shown=ctx->r4==0;__real_UIButtonImage_SetState(rdram,ctx);}
extern "C" void UIButtonImage_SetStateInsideRect(uint8_t* rdram,recomp_context* ctx){
    int x=int32_t(ctx->r5),y=int32_t(ctx->r6),right=int32_t(ctx->r7),bottom=MEM_W(0x10,ctx->r29);
    for(auto& b:promptButtons)if(b.rect.x>=x&&b.rect.x<=right&&b.rect.y>=y&&b.rect.y<=bottom)b.shown=ctx->r4==0;
    __real_UIButtonImage_SetStateInsideRect(rdram,ctx);
}

namespace {
void skyRender(uint8_t* rdram,recomp_context* ctx,void (*original)(uint8_t*,recomp_context*)) {
    bool course=false;Vec3 skyCenter{};
    if(requested.load()){auto& s=shared();std::lock_guard lock(s.mutex);course=s.game.course;skyCenter=s.game.cartPosition+Vec3{0,120,0};}
    uint32_t obj=uint32_t(ctx->r4),cam=word(rdram,mainCamera);
    uint32_t model=pointer(obj)?word(rdram,obj+0x48):0;
    // These draw callbacks also render real course blocks (including rocks).
    // Only the actual SkyBoxObject may follow the rider; terrain must retain
    // its block transform. Keep the dome independent of handheld lens motion.
    const uint32_t skyObject=word(rdram,uint32_t(section_addresses[12])+0x5248);
    {auto& s=shared();std::lock_guard lock(s.mutex);if(s.game.cinematic)course=false;}
    if(!course||obj!=skyObject||!pointer(model)||!pointer(cam)){original(rdram,ctx);return;}
    Vec3 saved=vector(rdram,model+0x1c);vector(rdram,model+0x1c,skyCenter);
    struct RestoreSky {uint8_t* rdram;uint32_t address;Vec3 saved;~RestoreSky(){vector(rdram,address,saved);}}restore{rdram,model+0x1c,saved};
    original(rdram,ctx);
}
}
extern "C" void drawSkyBox1Cycle(uint8_t* rdram,recomp_context* ctx){skyRender(rdram,ctx,__real_drawSkyBox1Cycle);}
extern "C" void drawSkyBox2Cycle(uint8_t* rdram,recomp_context* ctx){skyRender(rdram,ctx,__real_drawSkyBox2Cycle);}
