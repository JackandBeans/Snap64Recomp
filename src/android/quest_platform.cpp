#include <jni.h>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>
#include <filesystem>

extern "C" JNIEXPORT void JNICALL
Java_org_snap64_quest_QuestActivity_nativeSetDataDirectory(JNIEnv* env,jclass,jstring directory) {
    const char* path=env->GetStringUTFChars(directory,nullptr);
    setenv("SNAP_DATA_DIR",path,1);
    chdir(path);
    if(access("quest-stats.enable",F_OK)==0)setenv("SNAP_STATS","1",1);
#ifdef SNAP_QUEST_BENCHMARK
    std::filesystem::create_directories("benchmark/results");
    setenv("SNAP_QUEST_BENCHMARK","1",1);
    setenv("SNAP_REPLAY","benchmark/beach.inputs",1);
    if(auto* scene=std::fopen("benchmark/scene.txt","r")) {
        char name[16]{};std::fscanf(scene,"%15s",name);std::fclose(scene);
        if(std::string(name)=="intro") {
            if(auto* neutral=std::fopen("benchmark/intro.inputs","wb"))std::fclose(neutral);
            setenv("SNAP_REPLAY","benchmark/intro.inputs",1);
        }
    }
    unsetenv("SNAP_STATS");
    unsetenv("SNAP_QUEST_EYE_SIZE");
    if(auto* config=std::fopen("benchmark/eye-size.txt","r")) {
        unsigned width=0,height=0;
        if(std::fscanf(config,"%u %u",&width,&height)==2&&width&&height) {
            const auto size=std::to_string(width)+"x"+std::to_string(height);
            setenv("SNAP_QUEST_EYE_SIZE",size.c_str(),1);
        }
        std::fclose(config);
    }
    unsetenv("SNAP_QUEST_VERTEX_AUDIT");
    unsetenv("SNAP_QUEST_EARLY_CAPTURE");
    if(auto* config=std::fopen("benchmark/early-capture.txt","r")) {
        unsigned enabled=0;if(std::fscanf(config,"%u",&enabled)==1&&enabled==1)setenv("SNAP_QUEST_EARLY_CAPTURE","1",1);
        std::fclose(config);
    }
    if(auto* config=std::fopen("benchmark/vertex-audit.txt","r")) {
        unsigned enabled=0;if(std::fscanf(config,"%u",&enabled)==1&&enabled==1)setenv("SNAP_QUEST_VERTEX_AUDIT","1",1);
        std::fclose(config);
    }
    unsigned rate=90;
    if(auto* config=std::fopen("benchmark/refresh.txt","r")) {
        unsigned requested=0;if(std::fscanf(config,"%u",&requested)==1&&(requested==72||requested==80||requested==90))rate=requested;
        std::fclose(config);
    }
    setenv("SNAP_QUEST_REFRESH",rate==72?"72":rate==90?"90":"80",1);
#endif
    env->ReleaseStringUTFChars(directory,path);
}
