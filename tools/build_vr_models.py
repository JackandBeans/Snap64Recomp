"""Blender source for the VR camera, ZERO-ONE cockpit, apple and Pester Ball.
Run: blender --background --python tools/build_vr_models.py
All dimensions are meters; native axes X right, Y up, -Z forward.
"""
import bpy,bmesh,json,math,sys
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vr_model_validation import validate_camera_assembly
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/vr';OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
materials={}
COLORS={'yellow':(.96,.65,.065),'gold':(.65,.35,.025),'dark':(.055,.065,.08),'rubber':(.025,.03,.035),'silver':(.55,.6,.65),'body':(.23,.28,.34),'glass':(.035,.15,.22),'red':(.75,.06,.035),'green':(.07,.6,.24),'ivory':(.9,.91,.86),'trim':(.12,.16,.20),'lens':(.025,.065,.09),'coating':(.08,.32,.39),'seat':(.095,.12,.14)}
COLORS.update({'apple':(.78,.045,.025),'stem':(.22,.095,.027),'leaf':(.16,.43,.055),'leaf_vein':(.34,.57,.09),'pester_red':(.87,.045,.11),'pester_blue':(.055,.18,.72),'pester_orange':(.98,.33,.035),'pester_yellow':(.98,.80,.19),'pester_rim':(.82,.60,.08),'pester_purple':(.39,.12,.60)})
for name,color in COLORS.items():
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);materials[name]=m
for name in ('pester_red','pester_orange','pester_blue'):
 m=materials[name];m.diffuse_color=(*COLORS[name],.65);m.use_nodes=True
 bsdf=m.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Base Color'].default_value=(*COLORS[name],1);bsdf.inputs['Alpha'].default_value=.65
 m.surface_render_method='DITHERED'
parts={'camera':[],'zero_one':[],'apple':[],'pester_ball':[]};group='camera'
def vec(p):return Vector((p[0],-p[2],p[1]))
def finish(o,name,mat,bevel=0):
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
 o.name=name;o.data.materials.append(materials[mat]);parts[group].append(o)
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if bevel:
  mod=o.modifiers.new('Soft manufactured edges','BEVEL');mod.width=bevel;mod.segments=2
  bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)
 for face in o.data.polygons:face.use_smooth=True
 mod=o.modifiers.new('Weighted surface normals','WEIGHTED_NORMAL');mod.keep_sharp=True
 bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)
 return o
def box(name,p,size,mat,bevel=.005):
 bpy.ops.mesh.primitive_cube_add(size=1,location=vec(p));o=bpy.context.object;o.dimensions=(size[0],size[2],size[1]);return finish(o,name,mat,bevel)
def cylinder(name,p,r,depth,mat,axis=(0,0,-1),vertices=32,bevel=0):
 bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=depth,location=vec(p));o=bpy.context.object;o.rotation_mode='QUATERNION';o.rotation_quaternion=Vector((0,0,1)).rotation_difference(vec(axis).normalized());return finish(o,name,mat,bevel)
def tube(name,points,r,mat,resolution=1):
 closed=(Vector(points[0])-Vector(points[-1])).length<1e-6
 if closed:points=points[:-1]
 curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.bevel_depth=r;curve.bevel_resolution=resolution;curve.use_fill_caps=not closed
 sp=curve.splines.new('POLY');sp.points.add(len(points)-1)
 sp.use_cyclic_u=closed
 for q,p in zip(sp.points,points):q.co=(*vec(p),1)
 o=bpy.data.objects.new(name,curve);bpy.context.collection.objects.link(o);bpy.ops.object.select_all(action='DESELECT');bpy.context.view_layer.objects.active=o;o.select_set(True);bpy.ops.object.convert(target='MESH');o=bpy.context.object;return finish(o,name,mat)

def lathe(name,p,profile,mat,axis=(0,0,1),segments=64,knurl=0):
 """Closed radial section: real rim recesses, rounded tires and integrated ribs."""
 direction=Vector(axis).normalized();u=direction.cross(Vector((0,1,0)))
 if u.length<.01:u=direction.cross(Vector((0,0,1)))
 u.normalize();v=direction.cross(u);verts=[];faces=[]
 for along,radius in profile:
  for i in range(segments):
   a=math.tau*i/segments;r=radius+(knurl if i%2 else 0)
   verts.append(vec(Vector(p)+direction*along+r*(u*math.cos(a)+v*math.sin(a))))
 for j in range(len(profile)):
  for i in range(segments):
   k=(i+1)%segments;n=(j+1)%len(profile)
   faces.append((j*segments+i,j*segments+k,n*segments+k,n*segments+i))
 mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
 bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
 o=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(o);return finish(o,name,mat)

def ring(name,p,outer,inner,depth,mat,axis=(0,0,1),segments=48):
 return lathe(name,p,[(-depth/2,inner),(-depth/2,outer),(depth/2,outer),(depth/2,inner)],mat,axis,segments)

def screw(name,p,r,axis=(0,0,1)):
 cylinder(name,p,r,r*.3,'silver',axis,12)
 o=box(name+' slot',p,(r*1.35,r*.22,r*.06),'dark',0)
 o.rotation_mode='QUATERNION';o.rotation_quaternion=vec((0,0,1)).rotation_difference(vec(axis));o.location+=vec(axis)*(r*.17)

def surface(name,vertices,faces,mat,bevel=0):
 """Create a smooth closed surface in native game coordinates."""
 mesh=bpy.data.meshes.new(name);mesh.from_pydata([vec(p) for p in vertices],[],faces);mesh.update()
 bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
 o=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(o)
 return finish(o,name,mat,bevel)

def polar_surface(name,point,mat,rings=24,slices=48):
 # Unique poles avoid coincident vertices and zero-area export triangles.
 vertices=[point(0,0)]
 for j in range(1,rings):
  for i in range(slices):vertices.append(point(math.pi*j/rings,math.tau*i/slices))
 vertices.append(point(math.pi,0));faces=[]
 for i in range(slices):faces.append((0,1+i,1+(i+1)%slices))
 for j in range(rings-2):
  for i in range(slices):
   a=1+j*slices+i;b=1+j*slices+(i+1)%slices
   faces.append((a,a+slices,b+slices,b))
 for i in range(slices):faces.append((len(vertices)-1,1+(rings-2)*slices+(i+1)%slices,1+(rings-2)*slices+i))
 return surface(name,vertices,faces,mat)
# Real camera silhouette, with a side grip and unobstructed rear display.
box('Camera main casting',(0,0,0),(.174,.112,.078),'body',.012)
box('Rubber right palm grip',(.085,-.007,.005),(.038,.101,.090),'rubber',.015)
box('Left palm support',(-.085,-.007,.005),(.022,.097,.084),'rubber',.009)
box('Top prism housing',(0,.062,-.002),(.066,.032,.059),'body',.009)
box('Eyecup mounting neck',(0,.065,.032),(.032,.021,.025),'body',.003)
box('Rear eyecup',(0,.069,.046),(.043,.023,.020),'rubber',.005)
box('Eyepiece glass',(0,.069,.0565),(.028,.013,.003),'glass',.001)
box('Display recessed bezel',(-.008,-.004,.043),(.132,.094,.012),'rubber',.006)
# This rail embeds in the casting/grip and supports the complete rear button row.
# Its left edge stays outside the live display rectangle (right edge x=.050).
box('Rear control mounting rail',(.066,-.004,.038),(.019,.085,.026),'body',.004)
box('Front casting gasket',(0,0,-.033),(.165,.100,.015),'dark',.008)
box('Front metal faceplate',(0,.001,-.040),(.156,.090,.008),'silver',.007)
cylinder('Lens mount',(0,.003,-.049),.044,.013,'silver',vertices=64,bevel=.001)
cylinder('Lens barrel',(0,.003,-.087),.041,.073,'dark',vertices=64)
knurl=lathe('Integrated focus ring knurl',(0,.003,0),[(-.078,.039),(-.078,.043),(-.081,.044),(-.102,.044),(-.105,.043),(-.105,.039)],'rubber',segments=96,knurl=.0012)
for face in knurl.data.polygons:face.use_smooth=False
for z in (-.074,-.108):ring('Lens band',(0,.003,z),.042,.040,.003,'silver',segments=64)
lathe('Machined front lens lip',(0,.003,0),[(-.121,.034),(-.121,.041),(-.125,.043),(-.137,.043),(-.140,.040),(-.140,.035),(-.135,.033)],'silver')
ring('Optical recess',(0,.003,-.138),.035,.028,.003,'lens',segments=64)
cylinder('Optical glass',(0,.003,-.139),.0285,.0016,'glass',vertices=64)
ring('Lens coating',(0,.003,-.13995),.026,.0248,.0005,'coating',segments=64)
cylinder('Deep optical center',(0,.003,-.13995),.015,.0006,'lens',vertices=48)
for radius,start,end in ((.022,1.75,2.70),(.0195,1.9,2.50)):
 tube('Optical coating highlight',[(radius*math.cos(start+(end-start)*i/16),.003+radius*math.sin(start+(end-start)*i/16),-.1401) for i in range(17)],.00045,'coating')
for i in range(11):
 a=-.9+i*.18;o=box('Focus scale tick',(.0413*math.sin(a),.003+.0413*math.cos(a),-.114),(.0012,.0012,.005 if i%5==0 else .0027),'ivory',0);o.rotation_euler.y=a
box('Focus index',(0,.044,-.073),(.002,.0012,.005),'red',0)
cylinder('Shutter',(.064,.060,-.006),.010,.007,'red',(0,1,0),32,bevel=.001)
cylinder('Mode dial',(-.062,.06,0),.019,.012,'dark',(0,1,0),32)
for y in [-.026,0,.026]:cylinder('Rear button',(.068,y,.051),.004,.004,'silver',(0,0,1),20)
for x in [-.068,.068]:
 for y in [-.034,.034]:screw('Body fastener',(x,y,-.044),.0022,(0,0,-1))
for side in (-1,1):
 for i in range(5):box('Grip molded rib',(side*.086,-.034+i*.014,-.039),(.016,.003,.003),'dark',.001)
 tube('Strap eyelet',[(side*.084,.037,-.014),(side*.105,.037,-.014),(side*.105,.025,-.014),(side*.09,.025,-.014)],.002,'silver')
box('Prism front fascia',(0,.064,-.032),(.046,.018,.004),'dark',.003)
box('Flash window',(0,.066,-.0345),(.033,.008,.002),'ivory',.001)
for x in (-.010,-.005,0,.005,.010):box('Flash diffuser rib',(x,.066,-.0357),(.001,.008,.001),'silver',0)
box('Accessory shoe base',(0,.079,.002),(.027,.004,.025),'dark',.001)
for x in (-.012,.012):box('Accessory shoe rail',(x,.082,.002),(.003,.004,.025),'silver',.0006)
# Keep the runtime display rectangle and film digits unobstructed at z=.050/.052.
box('Display inner trim',(-.008,-.004,.049),(.120,.091,.001),'trim',.001)
for i in range(8):
 a=math.tau*i/8;o=box('Mode dial tick',(-.062+.012*math.sin(a),.0662,.012*math.cos(a)),(.0015,.0008,.004),'ivory' if i else 'red',0);o.rotation_euler.z=-a
cylinder('Shutter collar',(.064,.054,-.006),.012,.007,'silver',(0,1,0))
cylinder('Lens release button',(.053,-.026,-.0455),.005,.005,'dark',vertices=20)
box('Film door seam',(0,-.055,.003),(.116,.002,.043),'dark',.001)
box('Film door latch',(-.032,-.057,.007),(.023,.004,.012),'silver',.002)
# Original ZERO-ONE: yellow round tub, exposed wheels, curved overhead hoop.
group='zero_one'
# Continuous closed shell cross-section, including inner cockpit wall and lip.
rings=[(.50,.72,.23),(.59,.80,.27),(.66,.87,.37),(.705,.925,.55),(.72,.94,.64),(.713,.933,.695),(.69,.909,.724),(.66,.88,.73),(.625,.844,.716),(.60,.81,.68),(.565,.754,.40),(.55,.73,.35),(.48,.65,.29)]
verts=[];faces=[];segments=64
for rx,rz,y in rings:
 for i in range(segments):
  a=i*math.tau/segments;verts.append(vec((rx*math.sin(a),y,rz*math.cos(a)-.13)))
for j in range(len(rings)):
 for i in range(segments):faces.append((j*segments+i,j*segments+(i+1)%segments,((j+1)%len(rings))*segments+(i+1)%segments,((j+1)%len(rings))*segments+i))
mesh=bpy.data.meshes.new('formed shell');mesh.from_pydata(verts,[],faces);mesh.update();o=bpy.data.objects.new('ZERO-ONE yellow cockpit shell',mesh);bpy.context.collection.objects.link(o);bpy.context.view_layer.objects.active=o;finish(o,o.name,'yellow')
o.data.materials.append(materials['gold']);o.data.materials.append(materials['trim'])
for face in o.data.polygons:
 band=face.index//segments;face.material_index=1 if band in (0,1,12) else 2 if band>=9 else 0
for name,rx,rz,y,r,mat in [('Hull rub rail',.701,.919,.525,.014,'gold'),('Cockpit lip piping',.646,.866,.729,.009,'ivory')]:
 tube(name,[(rx*math.sin(math.tau*i/80),y,rz*math.cos(math.tau*i/80)-.13) for i in range(81)],r,mat)
box('Floor',(0,.29,-.05),(.93,.065,1.12),'dark',.05)
box('Seat cushion',(0,.46,.25),(.55,.16,.48),'rubber',.065)
box('Seat back',(0,.72,.48),(.55,.49,.12),'rubber',.07)
box('Seat plinth',(0,.375,.27),(.43,.12,.38),'trim',.025)
for x in (-.16,-.08,0,.08,.16):
 box('Cushion stitched panel',(x,.541,.25),(.068,.012,.34),'seat',.005)
 box('Backrest stitched panel',(x,.73,.412),(.068,.33,.018),'seat',.008)
for x in (-.22,.22):
 box('Footwell inset',(x,.327,-.37),(.30,.009,.37),'rubber',.025)
 for z in (-.50,-.43,-.36,-.29):box('Footwell traction strip',(x,.334,z),(.24,.010,.018),'trim',.004)
for side in [-1,1]:
 box('Seat rib',(side*.20,.72,.409),(.023,.33,.015),'body',.008)
 # Rear tire, axle, hub and deliberate tread blocks.
 x=side*.76;z=.34
 lathe('Rounded rear tire',(x,.27,z),[(-.115,.15),(-.115,.238),(-.089,.284),(-.057,.29),(.057,.29),(.089,.284),(.115,.238),(.115,.15)],'rubber',(1,0,0))
 cylinder('Yellow wheel hub',(x+side*.125,.27,z),.18,.022,'yellow',(1,0,0))
 cylinder('Hub cap',(x+side*.14,.27,z),.085,.028,'gold',(1,0,0))
 for i in range(24):
  a=math.tau*i/24
  o=box('Tire tread',(x,.27+.286*math.cos(a),z+.286*math.sin(a)),(.245,.038,.073),'dark',.008)
  o.rotation_euler.x=a
 tube('Suspension',[(side*.38,.30,.23),(side*.69,.26,.34)],.035,'silver')
 # Hoop follows sides and arches over the seat, behind the user's head.
points=[(-.59,.63,.40),(-.60,.98,.43)]
for i in range(33):
 a=math.pi-i*math.pi/32;points.append((.59*math.cos(a),1.02+.63*math.sin(a),.43))
points.extend([(.60,.98,.43),(.59,.63,.40)])
tube('Yellow protective arch',points,.033,'yellow',resolution=3)
for side in [-1,1]:
 cylinder('Hoop hinge',(side*.6,.99,.43),.057,.07,'gold',(0,0,1))
 cylinder('Hoop pivot bolt',(side*.6,.99,.39),.018,.012,'silver',(0,0,1))
box('Top hoop pad',(0,1.65,.43),(.32,.075,.105),'body',.025)
cylinder('Front tire',(0,.22,-.94),.265,.23,'rubber',(1,0,0),64)
for side in [-1,1]:cylinder('Front hub',(side*.13,.22,-.94),.16,.02,'yellow',(1,0,0))
for i in range(24):
 a=math.tau*i/24;o=box('Front tread',(0,.22+.263*math.cos(a),-.94+.263*math.sin(a)),(.25,.036,.069),'dark',.007);o.rotation_euler.x=a
# One continuous dashboard shell sweeps back into a rounded left control pod.
# Its flute cap sits near flush with the gauges instead of on a separate slab.
dash_outline=[(-.29,-.78),(.29,-.78),(.325,-.745),(.325,-.595),(.29,-.56),
              (-.125,-.56),(-.151,-.46),(-.182,-.426),(-.24,-.414),
              (-.292,-.431),(-.325,-.474),(-.325,-.745)]
def dashboard_shell(name,bottom,top,mat,expand=1,bevel=.018):
 outline=[(x*expand,-.60+(z+.60)*expand) for x,z in dash_outline];n=len(outline)
 vertices=[(x,y,z) for y in (bottom,top) for x,z in outline]
 faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]
 faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
 return surface(name,vertices,faces,mat,bevel)
dashboard_shell('Dashboard',.675,.765,'body')
flute_x,flute_z=-.24,-.49
ring('Flute button gasket',(flute_x,.766,flute_z),.058,.046,.006,'rubber',(0,1,0),48)
ring('Flute button bezel',(flute_x,.771,flute_z),.055,.047,.008,'silver',(0,1,0),48)
# The colored cap, original game icon and music indicator are rendered at runtime.
for i in range(3):
 cylinder('Dashboard gauge',(-.16+i*.16,.773,-.67),.044,.007,'silver',(0,1,0),32)
 cylinder('Gauge face',(-.16+i*.16,.778,-.67),.035,.003,'dark',(0,1,0),32)
for x,mat in [(.22,'red')]:
 cylinder('Dash switch collar',(x,.772,-.59),.026,.016,'silver',(0,1,0),24)
 cylinder('Dash switch',(x,.79,-.59),.022,.021,mat,(0,1,0),24)
cylinder('Front lamp housing',(0,.56,-1.035),.17,.09,'gold')
cylinder('Front lamp lens',(0,.56,-1.086),.13,.018,'ivory')
for x in [-.065,.065]:box('Lamp guard vertical',(x,.56,-1.105),(.018,.255,.018),'yellow',.005)
for y in [.49,.63]:box('Lamp guard horizontal',(0,y,-1.109),(.28,.018,.018),'yellow',.005)
for side in [-1,1]:
 for z in [-.53,-.28,0,.23]:
  x=side*.712*math.sqrt(1-((z+.13)/.932)**2);axis=Vector((x/.712**2,0,(z+.13)/.932**2)).normalized()
  screw('Hull rivet',(x,.59,z),.012,axis)
 # Integrated item wells retain the established seated reach locations.
 bin_z=.02 if side<0 else -.23
 cylinder('Dispenser cup',(.36,.675,bin_z),.095,.085,'body',(0,1,0),40)
 ring('Dispenser rim',(.36,.72,bin_z),.101,.078,.018,'gold',(0,1,0),40)
 cylinder('Dispenser well',(.36,.726,bin_z),.077,.009,'dark',(0,1,0),40)
box('Camera holster base',(.28,.865,-.38),(.20,.035,.12),'rubber',.016)
for x in [.185,.375]:box('Holster retaining lip',(x,.90,-.38),(.015,.055,.12),'gold',.008)
# Mechanical details use geometry and vertex colors supported by the VR renderer.
for side in (-1,1):
 x=side*.76;px=x+side*.143
 ring('Rear rim lip',(px,.27,.34),.178,.151,.010,'silver',(1,0,0))
 ring('Recessed wheel hub',(px+side*.007,.27,.34),.144,.088,.010,'trim',(1,0,0))
 ring('Tire sidewall bead',(x+side*.116,.27,.34),.231,.219,.005,'dark',(1,0,0))
 for i in range(6):
  a=math.tau*i/6;screw('Rear hub lug',(px+side*.016,.27+.116*math.cos(a),.34+.116*math.sin(a)),.012,(side,0,0))
 ring('Front wheel rim',(side*.143,.22,-.94),.154,.126,.008,'silver',(1,0,0))
 cylinder('Front axle cap',(side*.15,.22,-.94),.065,.02,'gold',(1,0,0),24)
 for i in range(5):
  a=math.tau*i/5;screw('Front hub lug',(side*.158,.22+.099*math.cos(a),-.94+.099*math.sin(a)),.010,(side,0,0))
 tube('Front fork',[(side*.17,.23,-.94),(side*.17,.41,-.85),(side*.12,.44,-.72)],.023,'silver')
 cylinder('Suspension boot',(side*.58,.29,.31),.055,.13,'dark',(1,0,0),24)
 cylinder('Hoop foot collar',(side*.592,.73,.41),.044,.07,'trim',(0,1,0),24)
 tube('Seat piping',[(side*.239,.60,.411),(side*.239,.88,.411),(side*.20,.917,.411)],.004,'body')
 box('Seat belt anchor',(side*.30,.48,.26),(.038,.06,.06),'silver',.008)
 bin_z=.02 if side<0 else -.23
 tube('Dispenser bracket',[(.53,.46,bin_z),(.36,.63,bin_z),(.36,.685,bin_z)],.025,'body')
 ring('Item bin color band',(.36,.706,bin_z),.096,.092,.009,'red' if side<0 else 'green',(0,1,0),40)
 box('Rear marker housing',(side*.30,.54,.706),(.15,.068,.04),'dark',.015)
 box('Rear marker lens',(side*.30,.54,.730),(.11,.040,.010),'red',.008)
 tube('Front bumper',[(side*.10,.36,-1.01),(side*.26,.39,-1.10),(side*.43,.43,-.96)],.021,'trim')
for x in (-.12,.12):box('Hoop pad strap',(x,1.65,.43),(.025,.080,.110),'rubber',.018)
tube('Camera holster support',[(.43,.60,-.38),(.35,.75,-.38),(.28,.847,-.38)],.025,'body')
dashboard_shell('Dashboard gasket',.670,.682,'rubber',1.018,.004)
for i in range(3):
 x=-.16+i*.16
 for tick in range(11):
  a=math.radians(-130+tick*26)
  o=box('Gauge graduation',(x+.029*math.sin(a),.780,-.67-.029*math.cos(a)),(.0022,.001,.006 if tick%5==0 else .0035),'red' if tick>8 else 'ivory',0);o.rotation_euler.z=a
 a=(-.7,.4,1.05)[i]
 tube('Gauge needle',[(x,.782,-.67),(x+.024*math.sin(a),.782,-.67-.024*math.cos(a))],.0013,'red')
 cylinder('Gauge pin',(x,.783,-.67),.004,.003,'silver',(0,1,0),12)
for x in (-.28,.28):screw('Dashboard fastener',(x,.765,-.70),.007,(0,1,0))
ring('Headlamp bezel',(0,.56,-1.083),.155,.129,.020,'silver',segments=64)
for x in (-.085,-.043,0,.043,.085):
 height=2*math.sqrt(.121**2-x*x);box('Headlamp lens flute',(x,.56,-1.096),(.003,height,.002),'silver',.001)
# Pokemon food: a hand-sized apple, with broad shoulders and five subtle lobes.
# The origin stays at the fruit center to retain the existing grasp/throw pose.
group='apple'
def apple_point(t,a):
 radius=.052*math.sin(t)*(1+.075*math.cos(t))*(1+.035*math.cos(5*a)*math.sin(t)**.6)
 y=.048*math.cos(t)-.013*math.exp(-(t/.26)**2)+.007*math.exp(-((math.pi-t)/.3)**2)
 return (radius*math.cos(a),y,radius*math.sin(a))
fruit=polar_surface('Apple lobed skin',apple_point,'apple',rings=28,slices=48)
# Continuous vertex colors give the peel a warm blush without texture dependencies.
colors=fruit.data.color_attributes.new(name='surface_color',type='FLOAT_COLOR',domain='POINT')
for vertex,color in zip(fruit.data.vertices,colors.data):
 x,y,z=vertex.co.x,vertex.co.z,-vertex.co.y;a=math.atan2(z,x)
 blush=.5+.5*math.sin(a+.6);stripe=.5+.5*math.sin(13*a+4*y)
 color.color=(.68+.17*blush,.024+.038*blush+.012*stripe,.018+.010*blush,1)
skin=materials['apple'];skin.use_nodes=True
skin_colors=skin.node_tree.nodes.new('ShaderNodeVertexColor');skin_colors.layer_name='surface_color'
skin.node_tree.links.new(skin_colors.outputs['Color'],skin.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
cylinder('Apple stem recess',(0,.035,0),.006,.0015,'stem',(0,1,0),20)
tube('Curved apple stem',[(0,.034,0),(-.002,.044,.001),(-.001,.055,0),(.003,.065,-.002),(.005,.070,-.003)],.0028,'stem',resolution=2)
# A solid leaf with a raised central fold and a tapered silhouette.
leaf_vertices=[]
for thickness in (-.0006,.0006):
 leaf_vertices.append((.006,.063+thickness,-.002))
 for j in range(1,8):
  t=j/8;width=.009*math.sin(math.pi*t)**.85
  for side in (-1,0,1):
   leaf_vertices.append((.006+.035*t,.063+.011*math.sin(math.pi*t)+.003*t+thickness+(.0015 if side==0 else 0),-.002+.008*t+side*width))
 leaf_vertices.append((.041,.066+thickness,.006))
leaf_faces=[];layer=23
for offset in (0,layer):
 leaf_faces.extend([(offset,offset+1,offset+2),(offset,offset+2,offset+3)])
 for j in range(6):
  for k in range(2):
   a=offset+1+j*3+k;leaf_faces.append((a,a+3,a+4,a+1))
 leaf_faces.extend([(offset+19,offset+22,offset+20),(offset+20,offset+22,offset+21)])
outline=[0]+[1+3*j for j in range(7)]+[22]+[3+3*j for j in reversed(range(7))]
for a,b in zip(outline,outline[1:]+outline[:1]):leaf_faces.append((a,b,b+layer,a+layer))
surface('Apple folded leaf',leaf_vertices,leaf_faces,'leaf')
tube('Leaf midrib',[(.006+.035*j/8,.065+.011*math.sin(math.pi*j/8)+.003*j/8,-.002+.008*j/8) for j in range(9)],.00055,'leaf_vein')
cylinder('Apple blossom scar',(0,-.041,0),.004,.0015,'stem',(0,1,0),12)

# Pester Ball: raised colored shells over a smaller yellow inner sphere. The
# visible yellow channels are recessed between the shells, never raised piping.
# Nominal grip radius remains about 55 mm.
group='pester_ball'
def pester_point(t,a,r=.0515):return (r*math.sin(t)*math.cos(a),r*math.sin(t)*math.sin(a),-r*math.cos(t))
polar_surface('Pester Ball recessed yellow core',pester_point,'pester_yellow',rings=24,slices=48)
for section,mat in enumerate(('pester_red','pester_orange','pester_blue')):
 # Boundary support rows form a small bevel down to the panel sidewalls.
 # Closed backs sink slightly into the core, so no floating edges or holes.
 rows,columns=20,14;vertices=[];faces=[]
 for back in (False,True):
  for j in range(rows+1):
   t=.44+(math.pi-.88)*j/rows
   for i in range(columns+1):
    a=section*math.tau/3+.14+(math.tau/3-.28)*i/columns
    edge=min(i,columns-i,j,rows-j)
    radius=.0512 if back else (.0544 if edge==0 else .056)
    vertices.append(pester_point(t,a,radius))
 layer=(rows+1)*(columns+1)
 for offset in (0,layer):
  for j in range(rows):
   for i in range(columns):
    a=offset+j*(columns+1)+i;faces.append((a,a+1,a+columns+2,a+columns+1))
 boundary=list(range(columns+1))+[j*(columns+1)+columns for j in range(1,rows+1)]+[rows*(columns+1)+i for i in reversed(range(columns))]+[j*(columns+1) for j in reversed(range(1,rows))]
 for a,b in zip(boundary,boundary[1:]+boundary[:1]):faces.append((a,b,b+layer,a+layer))
 panel=surface('Pester Ball raised '+mat.removeprefix('pester_')+' panel',vertices,faces,mat)
for side in (-1,1):
 axis=(0,0,side)
 lathe('Pester Ball recessed port bezel',(0,0,side*.0445),[(-.004,.017),(-.004,.023),(.001,.024),(.004,.021),(.004,.016),(.001,.015)],'pester_yellow',axis,segments=48)
 ring('Pester Ball inner port rim',(0,0,side*.050),.017,.0145,.002,'ivory',axis,48)
 polar_surface('Pester Ball purple port',lambda t,a:(.0145*math.sin(t)*math.cos(a),.0145*math.sin(t)*math.sin(a),side*(.0505+.003*math.cos(t))),'pester_purple',rings=10,slices=32)

# Export evaluated triangles with applied transforms and checked normals.
manifest={'units':'meters','axes':'X right Y up -Z forward','models':{}}
stats={}
for group,objects in parts.items():
 data=[];indices=[];unique={};alpha=[];shutter_vertices=set()
 for o in objects:
  bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
  bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7);bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
  o.data.calc_loop_triangles()
  for tri in o.data.loop_triangles:
   rgba=o.data.materials[o.data.polygons[tri.polygon_index].material_index].diffuse_color
   rgb=rgba[:3];opacity=round(float(rgba[3]),7)
   points=[o.data.vertices[i].co for i in tri.vertices]
   assert (points[1]-points[0]).cross(points[2]-points[0]).length>1e-12,(o.name,'degenerate')
   for li,vi in zip(tri.loops,tri.vertices):
    p=o.data.vertices[vi].co;n=o.data.corner_normals[li].vector
    assert abs(n.length-1)<.002
    uv=o.data.uv_layers.active.data[li].uv if o.data.uv_layers.active else (p.x,p.z)
    color=o.data.color_attributes.get('surface_color')
    tint=color.data[vi].color[:3] if color else rgb
    vertex=tuple(round(float(v),7) for v in (p.x,p.z,-p.y,n.x,n.z,-n.y,*tint,*uv))
    assert all(math.isfinite(v) for v in vertex),(o.name,'nonfinite export')
    key=(*vertex,opacity)
    if key not in unique:unique[key]=len(data)//11;data.extend(vertex);alpha.append(opacity)
    indices.append(unique[key])
    if o.name=="Shutter":shutter_vertices.add(unique[key])
   exported=[Vector(data[index*11:index*11+3]) for index in indices[-3:]]
   assert (exported[1]-exported[0]).cross(exported[2]-exported[0]).length>1e-12,(o.name,'degenerate rounded export')
 manifest['models'][group]={'vertices':data,'indices':indices}
 if shutter_vertices:manifest['models'][group]['shutter_vertices']=sorted(shutter_vertices)
 if any(a<1 for a in alpha):manifest['models'][group]['alpha']=alpha
 stats[group]={'triangles':len(indices)//3,'vertices':len(data)//11,'bounds':[[min(data[a::11]),max(data[a::11])] for a in range(3)]}
 assert stats[group]['triangles']<{'camera':24300,'zero_one':37940,'apple':4000,'pester_ball':9500}[group],(group,'triangle budget exceeded')
 print(group,stats[group],flush=True)
validate_camera_assembly(parts['camera'])
(OUT/'props.json').write_text(json.dumps(manifest,separators=(',',':'))+'\n')
from vr_projectile_assets import export as export_projectiles
export_projectiles(ROOT)
(ROOT/'build-win').mkdir(parents=True,exist_ok=True)
(ROOT/'build-win/vr-model-stats.json').write_text(json.dumps(stats,indent=2)+'\n')
bpy.context.preferences.filepaths.save_version=0
bpy.context.scene.unit_settings.system='METRIC'
# Named collections and separate review scenes keep each prop easy to edit in
# Blender without moving its runtime origin or mixing it with the other model.
collections={}
for model,objects in parts.items():
 collection=bpy.data.collections.new(model);bpy.context.scene.collection.children.link(collection);collections[model]=collection
 for o in objects:
  for old in list(o.users_collection):old.objects.unlink(o)
  collection.objects.link(o)
bpy.context.scene.name='VR Props Source'
# Portable individual models for reuse outside the custom JSON renderer.
for model in ('apple','pester_ball'):
 bpy.ops.object.select_all(action='DESELECT')
 for o in parts[model]:o.select_set(True)
 bpy.context.view_layer.objects.active=parts[model][0]
 bpy.ops.export_scene.gltf(filepath=str(OUT/(model+'.glb')),export_format='GLB',use_selection=True,export_yup=True,export_apply=True)
# Render the actual exported source geometry. Materials intentionally remain simple:
# the game consumes normals/colors, not roughness, metallic or transparency maps.
bpy.ops.object.camera_add();cam=bpy.context.object;bpy.context.scene.camera=cam;cam.data.type='ORTHO';cam.data.clip_start=.001
bpy.ops.object.light_add(type='AREA',location=(2,3,5));bpy.context.object.data.energy=650;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=5
bpy.ops.object.light_add(type='AREA',location=(-3,-2,3));bpy.context.object.data.energy=300;bpy.context.object.data.size=4
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True;scene.world.color=(.16,.16,.16);scene.render.resolution_percentage=100

def review(filename,model,position,target,scale,width=1200,height=1000):
 view=bpy.data.scenes.new(filename.removesuffix('.png'));view.collection.children.link(collections[model]);view.world=scene.world
 view.render.engine='CYCLES';view.cycles.samples=32;view.cycles.use_denoising=True;view.unit_settings.system='METRIC'
 camera=cam.copy();camera.data=cam.data.copy();camera.name=view.name+' camera';view.collection.objects.link(camera);view.camera=camera
 for light in scene.objects:
  if light.type=='LIGHT':view.collection.objects.link(light)
 camera.location=vec(position);camera.rotation_euler=(vec(target)-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=scale
 view.render.resolution_x=width;view.render.resolution_y=height;view.render.resolution_percentage=100;view.render.filepath=str(ROOT/'build-win'/filename)
 if '--no-render' not in sys.argv:bpy.ops.render.render(write_still=True,scene=view.name)

review('vr-model-review.png','zero_one',(2.8,2.4,-3.8),(0,.79,-.12),2.75,1400,1200)
review('vr-cockpit-review.png','zero_one',(1.8,2.6,2.5),(0,.73,-.15),2.65,1400,1200)
review('vr-dashboard-review.png','zero_one',(-.05,1.35,.04),(-.10,.755,-.58),.80,1200,1000)
review('vr-camera-front-review.png','camera',(.29,.20,-.40),(0,.006,-.043),.32)
review('vr-camera-review.png','camera',(.30,.20,.38),(0,.012,-.025),.32)
review('vr-camera-side-review.png','camera',(.38,.012,-.035),(0,.012,-.035),.275)
review('vr-apple-review.png','apple',(.18,.13,-.23),(0,.016,0),.165)
review('vr-pester-ball-review.png','pester_ball',(.14,.10,-.25),(0,0,0),.155)
bpy.context.window.scene=bpy.data.scenes['vr-model-review']
for screen in bpy.data.screens:
 for area in screen.areas:
  if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.shading.color_type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'snap_vr_props.blend'))
