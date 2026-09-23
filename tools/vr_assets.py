"""Convert DramaticShape's numeric hand rig into portable JSON (no Lua execution).

The source describes CC0 Godot XR Tools meshes with Ash-style glove colors.
Only numeric arrays, bone names, and rotations are imported, never source code.
"""
import argparse
import json
import math
import re
from pathlib import Path

def convert(source):
    text=Path(source).read_text(encoding='utf-8')
    hands={}
    for side in ('left','right'):
        start=text.index('  '+side+' = {')
        end=text.find('  right = {',start+1) if side=='left' else len(text)
        block=text[start:end]
        data={}
        data['bones']=re.findall(r'"([A-Za-z_]+)"',re.search(r'bones = \{([^}]+)',block)[1])
        data['parent']=[int(v) for v in re.findall(r'\d+',re.search(r'parent = \{([^}]+)',block)[1])]
        arrays=dict(re.findall(r'(\w+)\s*=\s*\[\[(.*?)\]\]',block,re.S))
        for name in ('rest','ibm','verts','tris'):
            data[name]=[float(v) for v in arrays.pop(name).split()]
        data['tris']=[int(v)-1 for v in data['tris']]
        data['poses']={k:[float(v) for v in v.split()] for k,v in arrays.items()}
        n=len(data['bones']);nv=len(data['verts'])//15
        assert len(data['rest'])==n*7 and len(data['ibm'])==n*12
        assert len(data['verts'])==nv*15 and len(data['tris'])%3==0
        assert all(0<=i<nv for i in data['tris'])
        assert all(math.isfinite(v) for key in ('rest','ibm','verts') for v in data[key])
        for i in range(nv):
            v=data['verts'][i*15:(i+1)*15]
            assert abs(math.sqrt(sum(x*x for x in v[3:6]))-1)<0.002, "non-unit vertex normal"
            assert abs(sum(v[11:15])-1)<0.002
            assert all(1<=j<=n for j,w in zip(v[7:11],v[11:15]) if w>0)
        for p in ('open','fist','grip_1','grip_2','grip_3','grip_4'):
            assert len(data['poses'][p])==n*4,p
        hands[side]=data
    return hands

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source');ap.add_argument('--output',default='assets/vr/hands.json')
    args=ap.parse_args();out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
    hands=convert(args.source);out.write_text(json.dumps(hands,separators=(',',':'))+'\n',encoding='utf-8')
    print(f'Validated and exported {len(hands)} hand rigs to {out}')
