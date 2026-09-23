#include "common.h"
#include "vr_pointer.inc"
extern GObj *D_800E82CC_A0F85C, *D_800E82DC_A0F86C;
extern u8 D_800E82E4_A0F874;
extern u32 D_800E82E8_A0F878;
extern s8 D_800E82ED_A0F87D;
void func_800E3240_A0A7D0(GObj*);
void func_800E3C7C_A0B20C(GObj*);
void func_800E18E0_A08E70(SObj*,u8,u8,u8);
void func_800E18AC_A08E3C(SObj*,u8);
u8 func_800E33C8_A0A958(GObj**);
s32 func_800E37E8_A0AD78(s32,s8);
void func_800E1AEC_A0907C(void);
void func_800E1AD4_A09064(void);
void func_800E1B78_A09108(u8);
void func_800E1CAC_A0923C(void);
s32 func_800E3E28_A0B3B8(void) {
    SObj* sobj2;
    SObj* sobj1;
    SObj* sobj0;
    GObj* gobj;
    s32 flags;
    s32 ret;
    s32 cond;
    s32 one;

    gobj = D_800E82DC_A0F86C;
    sobj0 = gobj->data.sobj;
    sobj1 = sobj0->next;
    sobj2 = sobj1->next;

    ohEndAllObjectProcesses(D_800E82CC_A0F85C);
    func_800E18E0_A08E70(D_800E82CC_A0F85C->data.sobj, 0xFF, 0xFF, 0xFF);
    func_800E18AC_A08E3C(sobj0, true);
    func_800E18AC_A08E3C(sobj2, true);
    func_800E18AC_A08E3C(sobj1, true);
    func_800E18E0_A08E70(sobj2, 0x80, 0x80, 0x80);
    func_800E18E0_A08E70(sobj1, 0x80, 0x80, 0x80);
    cond = false;
    gobj->userData = NULL;
    omCreateProcess(gobj, func_800E3C7C_A0B20C, 0, 1);
    auSetBGMVolume(1, 0x7F00);
    auPlaySong(1, 0x23);
    auSetBGMVolumeSmooth(0, 0x4000, 30);
    ohWait(30);

    one = 1;

    while (true) {
        flags = func_800AA38C(0)->pressedButtons;
        if(snap_vr_sprite(sobj2) || snap_vr_sprite(sobj1)) {
            cond=snap_vr_sprite(sobj2);
            if((gobj->userData!=NULL)!=cond) {
                ohEndAllObjectProcesses(gobj);
                func_800E18E0_A08E70(sobj1,0x80,0x80,0x80);
                func_800E18E0_A08E70(sobj2,0x80,0x80,0x80);
                gobj->userData=cond?(void*)1:NULL;
                omCreateProcess(gobj,func_800E3C7C_A0B20C,0,1);
            }
        }
        if (flags & 0x4000) {
            cond = false;
            break;
        } else {
            if (flags & 0x80000) {
                if (gobj->userData == NULL) {
                    auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
                    cond = true;
                    ohEndAllObjectProcesses(gobj);
                    gobj->userData = (void*) one;
                    func_800E18E0_A08E70(sobj1, 0x80, 0x80, 0x80);
                    omCreateProcess(gobj, func_800E3C7C_A0B20C, 0, 1);
                }
            } else if (flags & 0x40000) {
                if (gobj->userData == (void*) one) {
                    auPlaySoundWithParams(0x41, 0x7FFF, 0x40, 1.0f, 0);
                    cond = false;
                    ohEndAllObjectProcesses(gobj);
                    gobj->userData = NULL;
                    func_800E18E0_A08E70(sobj2, 0x80, 0x80, 0x80);
                    omCreateProcess(gobj, func_800E3C7C_A0B20C, 0, 1);
                }
            } else if (flags & (0x8000 | 0x1000)) {
                break;
            }

            ohWait(one);
        }
    }

    if (cond) {
        auPlaySoundWithParams(0x42, 0x7FFF, 0x40, 1.0f, 0);
        func_800E1CAC_A0923C();
        ret = 6;
    } else {
        ohEndAllObjectProcesses(gobj);
        func_800E18AC_A08E3C(sobj0, false);
        func_800E18AC_A08E3C(sobj2, false);
        func_800E18AC_A08E3C(sobj1, false);
        auPlaySoundWithParams(0x43, 0x7FFF, 0x40, 1.0f, 0);
        auSetBGMVolumeSmooth(0, 0x7FFF, 30);
        ohWait(30);
        ret = 3;
    }
    ohWait(1);
    return ret;
}
