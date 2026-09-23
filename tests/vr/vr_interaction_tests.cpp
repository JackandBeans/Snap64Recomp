#include "vr_interaction.h"
#include "vr_menu.h"
#include "vr_messages.h"
#include "vr_transparency.h"
#include <cstdlib>
#include <iostream>
using namespace snap::vr;
void check(bool v,const char* message) { if(!v){std::cerr<<message<<'\n';std::exit(1);} }
bool near(float a,float b) {return std::abs(a-b)<0.001f;}
int main() {
    Interaction vr; Tracking t; t.focused=t.headValid=true; t.head.position={0,1.2f,0};
    GameState g; g.course=g.apples=g.pesterBalls=true; g.epoch=1;
    auto f=vr.update(t,g);
    check(near(f.head.position.y,120),"seated eye height");
    t.head.position.x=.1f; f=vr.update(t,g);
    check(near(f.head.position.x,-10),"6DoF lean, correct game forward axis");
    g.cartYaw=pi/2; f=vr.update(t,g);
    check(near(f.head.position.z,10)&&near(f.head.position.y,120),"cart yaw, level horizon");
    vr.recenter(t); f=vr.update(t,g); check(near(f.head.position.x,0)&&near(f.head.position.z,0),"recenter");
    // Fresh session for dispenser interaction.
    Interaction items; t={}; t.focused=t.headValid=true;t.head.position={0,1.2f,0};g.cartYaw=0;
    items.update(t,g); t.hands[0].tracked=true;t.hands[0].grip.position=Interaction::appleBin;
    t.seconds=.01; items.update(t,g);t.seconds=.02;t.hands[0].squeeze=1;
    f=items.update(t,g);check(f.held[0]==Held::Apple,"grab unlocked apple");
    t.seconds=.06;t.hands[0].grip.position.z-=.3f;items.update(t,g);
    t.seconds=.09;t.hands[0].grip.position.z-=.3f;t.hands[0].squeeze=0;
    f=items.update(t,g);check(f.throws.size()==1&&f.throws[0].velocity.z>0,"physical forward throw");
    t.seconds=.5;t.hands[0].grip.position=Interaction::appleBin;items.update(t,g);
    t.seconds=.51;t.hands[0].squeeze=1;items.update(t,g);
    t.seconds=.52;t.hands[0].tracked=false;t.hands[0].squeeze=0;
    f=items.update(t,g);check(f.throws.empty()&&f.held[0]==Held::None,"tracking loss never throws");
    t.focused=false;f=items.update(t,g);check(f.pause,"focus loss requests pause once");
    f=items.update(t,g);check(!f.pause,"no repeated pause toggle");
    Pose p{yaw(.7f),{1,2,3}};auto id=compose(p,inverse(p));check(length(id.position)<.001f,"pose inverse");
    auto m=projection({-.9f,.7f,.8f,-.6f},.05f,100);check(m[8]!=0&&m[9]!=0,"asymmetric eye frustum");
    Interaction camera;t={};t.focused=t.headValid=true;t.head.position={0,1.2f,0};g={};g.course=true;g.epoch=2;
    camera.update(t,g);t.hands[1].tracked=true;t.hands[1].grip.position=Interaction::cameraDock;t.seconds=.01;camera.update(t,g);
    t.seconds=.02;t.hands[1].squeeze=1;f=camera.update(t,g);check(f.cameraHeld,"camera grab");
    t.seconds=.03;t.hands[1].trigger=1;f=camera.update(t,g);check(f.shutter,"trigger shutter edge");
    t.seconds=.04;f=camera.update(t,g);check(!f.shutter,"held trigger does not repeat photos");
    g.film=0;t.seconds=.05;t.hands[1].trigger=0;camera.update(t,g);t.seconds=.06;t.hands[1].trigger=1;f=camera.update(t,g);check(!f.shutter,"out of film");
    t.seconds=.07;t.hands[1].squeeze=0;t.hands[1].grip.position={2,2,2};f=camera.update(t,g);check(!f.cameraHeld&&f.throws.empty(),"camera returns to holster on distant release");
    t.seconds=.08;t.hands[1].grip.position=Interaction::appleBin;camera.update(t,g);t.seconds=.09;t.hands[1].squeeze=1;f=camera.update(t,g);check(f.held[1]==Held::None,"locked apples cannot spawn");
    t.seconds=.1;t.hands[1].squeeze=0;camera.update(t,g);g.apples=true;g.itemReady=false;t.seconds=.11;t.hands[1].squeeze=1;f=camera.update(t,g);check(f.held[1]==Held::None,"game cooldown gates pickup");
    t.seconds=.12;t.hands[1].squeeze=0;camera.update(t,g);g.itemReady=true;t.seconds=.13;t.hands[1].squeeze=1;f=camera.update(t,g);check(f.held[1]==Held::Apple,"right hand apple");
    g.epoch++;t.seconds=.14;t.hands[1].squeeze=0;f=camera.update(t,g);check(f.throws.empty()&&f.held[1]==Held::None,"course transition cancels held item");
    Interaction drops;t={};t.focused=t.headValid=true;t.head.position={0,1.2f,0};
    g={};g.course=g.apples=true;g.epoch=1;g.cartVelocity={40,0,-20};drops.update(t,g);
    t.hands[0].tracked=true;t.hands[0].grip.position=Interaction::appleBin;t.seconds=.01;drops.update(t,g);
    t.hands[0].squeeze=1;t.seconds=.03;drops.update(t,g);
    t.hands[0].squeeze=0;t.seconds=.07;f=drops.update(t,g);
    check(f.throws.size()==1&&length(f.throws[0].velocity-g.cartVelocity)<.001f,"stationary drop inherits cart velocity");
    // A tracking gap with the grip still squeezed must not re-arm the hand.
    t.seconds=.5;t.hands[0].squeeze=1;t.hands[0].tracked=false;drops.update(t,g);
    t.seconds=.52;t.hands[0].tracked=true;f=drops.update(t,g);check(f.held[0]==Held::None,"tracking recovery cannot grab on a held grip");
    t.seconds=.54;t.hands[0].squeeze=0;f=drops.update(t,g);check(f.throws.empty(),"tracking recovery release cannot throw");
    // The handheld camera follows the controller even when the head turns away.
    Interaction independent;t={};t.focused=t.headValid=true;t.head.position={0,1.2f,0};g={};g.course=true;g.epoch=1;independent.update(t,g);
    t.hands[0].tracked=true;t.hands[0].grip.position=Interaction::cameraDock;t.seconds=.01;independent.update(t,g);
    t.hands[0].squeeze=1;t.seconds=.03;f=independent.update(t,g);Pose initialLens=f.lens;
    t.head.orientation=yaw(pi/2);t.head.position.x=.2f;t.seconds=.05;f=independent.update(t,g);
    check(length(f.lens.position-initialLens.position)<.001f&&near(f.lens.orientation.y,initialLens.orientation.y),"head movement does not aim the camera");
    check(length(compose(independent.cartPose(g),f.lensInCart).position-f.lens.position)<.001f,"lens rebases to current cart pose");
    for(int row=0;row<19;row++)for(int column=0;column<5;column++) {
        auto cell=nameCell(25.f+13*column,22.f+10*row);
        check(cell.x==column&&cell.y==row,"pointer maps directly to every name-grid key");
    }
    check(!nameCell(150,80).valid()&&!nameCell(25,10).valid(),"outside name grid has no selection");
    check(nameCell(25,212).x==0&&nameCell(38,212).x==1&&nameCell(63,212).x==2,"name editing buttons");
    Pose mesh=handMeshPose(Pose{});Vec3 up=rotate(mesh.orientation,{0,1,0});
    check(near(up.z,-1)&&near(up.y,0)&&length(mesh.position)<.001f,"hands rotate ninety degrees forward about wrist");
    MenuRect row{22,77,62,13};
    check(row.contains({50,82,true})&&row.direction({50,82,true})==0,"hovering a selected menu row never scrolls");
    check(row.direction({50,106,true})==0x20000&&row.direction({50,58,true})==0x10000,"menu navigation follows actual row bounds");
    check(row.direction({210,170,true})==0,"lab photo area does not steer the side list");
    MenuRect photo{107,56,60,45};
    check(photo.direction({200,70,true})==0x40000&&photo.direction({90,70,true})==0x80000,"photo grid points navigate horizontally");
    check(photo.direction({120,130,true})==0x20000&&!photo.contains({0,0,false}),"photo grid vertical navigation and missing pointer");
    for(int side=0;side<2;side++) {
        Interaction grip;Tracking sample{};GameState course{};
        sample.focused=sample.headValid=true;sample.head.position={0,1.2f,0};course.course=true;course.epoch=1;
        grip.update(sample,course);sample.hands[side].tracked=true;
        sample.hands[side].grip.position=Interaction::cameraDock;
        sample.hands[side].aim.orientation=yaw(.4f);sample.seconds=.01;grip.update(sample,course);
        sample.hands[side].squeeze=1;sample.seconds=.03;auto held=grip.update(sample,course);
        Pose body=compose(held.lens,Pose{{},{0,0,14}}),wrist=handMeshPose(held.hands[side]);
        Vec3 socket{side==0?-.087f:.092f,-.012f,.010f};
        Vec3 palm=wrist.position+rotate(wrist.orientation,Vec3{side==0?.025f:-.025f,.015f,-.055f}*100);
        check(length(body.position+rotate(body.orientation,socket*100)-palm)<.001f,"camera side socket attaches to either palm");
        auto aim=grip.toWorld(sample.hands[side].aim,course);
        check(length(rotate(held.lens.orientation,{0,0,-1})-rotate(aim.orientation,{0,-1,0}))<.001f,"camera rotates ninety degrees forward at the palm, including its optical pose");
        sample.hands[1-side].tracked=true;sample.hands[1-side].squeeze=1;
        sample.hands[1-side].grip.position=sample.hands[side].grip.position;
        sample.hands[side].aim.orientation=yaw(.6f);sample.seconds=.05;
        held=grip.update(sample,course);body=compose(held.lens,Pose{{},{0,0,14}});
        check(length(body.position+rotate(body.orientation,socket*100)-palm)<.001f,"two-hand smoothing preserves the camera palm attachment");

    }
    GameState previous,current;previous.course=current.course=true;previous.epoch=current.epoch=4;
    previous.cartPosition={100,0,0};current.cartPosition={120,0,0};previous.cartYaw=pi-.1f;current.cartYaw=-pi+.1f;
    auto middle=interpolateCart(previous,current,.5f);
    check(near(middle.cartPosition.x,110)&&near(middle.cartYaw,pi),"cart interpolation uses the short yaw arc and simulation interval");
    middle=interpolateCart(previous,current,.5f,{20,0,0});check(near(middle.cartPosition.x,120),"block rebase does not move the rider backwards");
    current.epoch++;check(near(interpolateCart(previous,current,.5f).cartPosition.x,120),"new course never interpolates from old course");
    check(messageText("Press \\z to aim.")=="GRIP THE CAMERA TO AIM.","VR tutorial describes the camera grip");
    check(messageText("Press \\a to shoot.")=="PULL THE CAMERA TRIGGER TO SHOOT.","VR tutorial describes the shutter");
    check(messageText("\\#S1Hello")=="Hello","message formatting commands are not displayed");
    auto lines=messageLines("One two three\nFour five",7);check(lines.size()==4&&lines[0]=="One two"&&lines[1]=="three"&&lines[2]=="Four","tutorial panel wraps words and explicit lines");
    auto alphaOrder=transparentIndices({{0,{0,0,-1}},{3,{0,0,-5}},{6,{100,0,-2}}},{});
    check(alphaOrder==std::vector<unsigned>({3,4,5,6,7,8,0,1,2}),"transparency draws far to near by eye depth, not radial distance");
    check(transparentIndices({},{}).empty(),"opaque-only frames need no transparent indices");
    const std::vector<TransparentTriangle> stereoPanels{{0,{-.2f,0,-2}},{3,{.2f,0,-2}}};
    check(transparentIndices(stereoPanels,Pose{yaw(.1f),{-.032f,0,0}})[0]==0&&
          transparentIndices(stereoPanels,Pose{yaw(-.1f),{.032f,0,0}})[0]==3,"transparency ordering is computed separately for oriented stereo eyes");
    check(transparentIndices(stereoPanels,{})[0]==0,"equal-depth transparent triangles keep stable order");
    check(transparentIndices({{0,{7,3,4}},{3,{3,3,5}}},Pose{yaw(pi/2),{8,3,5}})[0]==3,"transparency follows translated and rotated viewer");
    std::cout<<"VR pose, interaction and transparency checks passed\n";
}
