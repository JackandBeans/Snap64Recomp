#include <jni.h>
#include <cstdlib>
#include <unistd.h>

extern "C" JNIEXPORT void JNICALL
Java_org_snap64_quest_QuestActivity_nativeSetDataDirectory(JNIEnv* env,jclass,jstring directory) {
    const char* path=env->GetStringUTFChars(directory,nullptr);
    setenv("SNAP_DATA_DIR",path,1);
    chdir(path);
    if(access("quest-stats.enable",F_OK)==0)setenv("SNAP_STATS","1",1);
    env->ReleaseStringUTFChars(directory,path);
}
