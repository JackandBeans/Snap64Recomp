/* 320x240 panel hit tests. Published by the host input bridge; zero in desktop mode. */
#define VR_POINT_MAGIC (*(volatile u32*)0x80C00F00)
#define VR_POINT_X (*(volatile f32*)0x80C00F04)
#define VR_POINT_Y (*(volatile f32*)0x80C00F08)
/* Published at overlay load, independent of pointer focus/tracking. */
#define VR_MODE_ENABLED (*(volatile u32*)0x80C00F0C)
static s32 snap_vr_hit(f32 x,f32 y,f32 w,f32 h) {
    return VR_POINT_MAGIC==0x56525054 && VR_POINT_X>=x-3 && VR_POINT_X<x+w+3 && VR_POINT_Y>=y-3 && VR_POINT_Y<y+h+3;
}
static s32 snap_vr_sprite(SObj* s) {
    return s!=NULL && snap_vr_hit(s->sprite.x,s->sprite.y,s->sprite.width*s->sprite.scalex,s->sprite.height*s->sprite.scaley);
}
static s32 snap_vr_row(s32 current,s32 top,s32 visible,s32 count) {
    s32 row;
    if(VR_POINT_MAGIC!=0x56525054 || VR_POINT_X<40 || VR_POINT_X>285 || VR_POINT_Y<70)return current;
    row=(s32)((VR_POINT_Y-70)/16);
    return row>=0 && row<visible && row+top<count?row+top:current;
}

static void snap_vr_header(void) {
    if((gContInputPressedButtons & A_BUTTON) && snap_vr_hit(226,40,58,14))
        gContInputPressedButtons=(gContInputPressedButtons & ~A_BUTTON)|B_BUTTON;
}
