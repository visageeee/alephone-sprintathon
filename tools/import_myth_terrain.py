#!/usr/bin/env python3
"""Convert a Myth2ools Y-up OBJ + BMP into an experimental Sprintathon level.
Python 3 standard library only. No Myth archive/tag extraction is performed.
"""
import argparse, json, math, pathlib, shutil, struct, zlib

WU=1024
NAME='Myth terrain import'
def shorts(*values): return struct.pack('>'+'h'*len(values), *values)
def name_bytes(name,n): return name.encode('ascii')[:n-1].ljust(n,b'\0')

def read_obj(path):
    if path.stat().st_size>16*1024*1024: raise ValueError('OBJ exceeds 16 MiB input budget')
    vertices=[]; uvs=[]; faces=[]
    for lineno,line in enumerate(path.read_text().splitlines(),1):
        parts=line.split('#',1)[0].split()
        if not parts: continue
        k,*args=parts
        if k in ('v','vt'):
            n=3 if k=='v' else 2
            if len(args)<n: raise ValueError(f'Incomplete {k} on line {lineno}')
            p=tuple(map(float,args[:n]))
            if not all(math.isfinite(v) for v in p): raise ValueError('Non-finite OBJ coordinate')
            (vertices if k=='v' else uvs).append(p)
        elif k=='f':
            if len(args)!=3: raise ValueError('Only triangulated OBJ exports are supported')
            f=[]
            for arg in args:
                indices=arg.split('/')
                if len(indices)<2 or not indices[1]: raise ValueError('Terrain needs UVs on every corner')
                v,t=int(indices[0])-1,int(indices[1])-1
                if not (0<=v<len(vertices) and 0<=t<len(uvs)): raise ValueError('Only valid positive OBJ indices are supported')
                f.append((v,t))
            faces.append(f)
        elif k not in ('vn','o','g','s','mtllib','usemtl'): raise ValueError(f'Unsupported OBJ record: {k}')
    if not faces or len(faces)>100000 or len(vertices)>100000 or len(uvs)>100000: raise ValueError('Invalid mesh size')
    if any(not 0<=v<=1 for uv in uvs for v in uv): raise ValueError('Colour-map UVs must be in [0,1]')
    return vertices,uvs,faces

def height_at(vertices,faces,x,y):
    for f in faces:
        a,b,c=(vertices[i] for i,t in f)
        det=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
        if abs(det)<1e-12: continue
        u=((x-a[0])*(c[1]-a[1])-(y-a[1])*(c[0]-a[0]))/det
        v=((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]))/det
        if min(u,v,1-u-v)>=-1e-8: return a[2]+u*(b[2]-a[2])+v*(c[2]-a[2])
    raise ValueError('No terrain below the test start position')

def make_map(width,height,spawn):
    # Integer grid endpoints, max eight-WU edges, shared lines. CCW in numeric
    # XY corresponds to the engine's clockwise order (screen Y points down).
    nx=math.ceil(width/8);ny=math.ceil(height/8)
    xs=[round((-width/2+width*i/nx)*WU) for i in range(nx+1)]
    ys=[round((-height/2+height*i/ny)*WU) for i in range(ny+1)]
    points=[(x,y) for y in ys for x in xs]
    polys=[];lines=[];lookup={}
    for y in range(ny):
        for x in range(nx):
            corners=[y*(nx+1)+x,y*(nx+1)+x+1,(y+1)*(nx+1)+x+1,(y+1)*(nx+1)+x]
            pid=len(polys); edges=[]
            for a,b in zip(corners,corners[1:]+corners[:1]):
                key=tuple(sorted((a,b)))
                if key not in lookup:
                    lookup[key]=len(lines); lines.append([a,b,pid,-1,-1,-1])
                else:
                    line=lines[lookup[key]]
                    if line[:2]!=[b,a] or line[3]!=-1: raise ValueError('Non-manifold grid')
                    line[3]=pid
                edges.append(lookup[key])
            polys.append((corners,edges))
    floor,ceiling=0,12*WU
    sides=[]
    for lid,line in enumerate(lines):
        a,b,owner,other,_,_=line
        if other!=-1: continue
        line[4]=len(sides)
        # A full landscape boundary: solid for movement, outdoor sky visually.
        side=shorts(0,0)+shorts(0,0,27<<8)+shorts(0,0,-1)*2+bytes(16)
        side+=shorts(0,0,9,0,0,owner,lid,0,0,0)+struct.pack('>i',0)+bytes(2)
        assert len(side)==64
        sides.append(side)
    linebytes=b''
    for a,b,owner,other,side,back in lines:
        length=round(math.dist(points[a],points[b]))
        linebytes+=shorts(a,b,0x4000 if other==-1 else 0x2000,length,floor,ceiling,side,back,owner,other)+bytes(12)
    polybytes=b''
    for pid,(corners,edges) in enumerate(polys):
        adjacent=[];sideidx=[]
        for lid in edges:
            a,b,owner,other,side,back=lines[lid]
            adjacent.append(other if owner==pid else owner);sideidx.append(side if owner==pid else back)
        p=shorts(0,0,0,4)+shorts(*(corners+[-1]*4))+shorts(*(edges+[-1]*4))
        p+=shorts(17<<8,27<<8,floor,ceiling,0,0)+struct.pack('>i',0)
        p+=shorts(-1,0,0,0,0,9)+shorts(*(adjacent+[-1]*4))
        cx=round(sum(points[i][0] for i in corners)/4);cy=round(sum(points[i][1] for i in corners)/4)
        p+=shorts(0,0,cx,cy)+shorts(*(sideidx+[-1]*4))
        p+=shorts(0,0,0,0,-1,0,-1,-1,-1,0)
        assert len(p)==128
        polybytes+=p
    sx,sy,sz=spawn
    ix=next(i for i in range(nx) if xs[i]<=sx<=xs[i+1]);iy=next(i for i in range(ny) if ys[i]<=sy<=ys[i+1])
    player=shorts(3,0,0,iy*nx+ix,sx,sy,sz,0)
    light=shorts(0,1,0)+(shorts(0,30,0)+struct.pack('>ii',65536,0))*6+shorts(0)+bytes(8)
    assert len(light)==100
    info=shorts(0,0,0,0,0)+bytes(8)+name_bytes(NAME,66)+struct.pack('>I',1)
    assert len(info)==88
    # Item/monster frequencies default to none (two sets of 64 definitions).
    placement=(shorts(0,0,0,0,0)+struct.pack('>H',0))*128
    tags=[(b'PNTS',b''.join(shorts(*p) for p in points)),(b'LINS',linebytes),(b'SIDS',b''.join(sides)),
          (b'POLY',polybytes),(b'LITE',light),(b'OBJS',player),(b'Minf',info),(b'plac',placement)]
    payload=b''
    for i,(tag,data) in enumerate(tags):
        nextoffset=len(payload)+16+len(data) if i<len(tags)-1 else 0
        payload+=struct.pack('>4siii',tag,nextoffset,len(data),0)+data
    header=struct.pack('>hh64sIihhhhI',4,2,name_bytes('Myth Terrain.sceA',64),0,128+len(payload),1,0,16,10,0)+bytes(40)
    assert len(header)==128
    wad=bytearray(header+payload+struct.pack('>iih',128,len(payload),0))
    struct.pack_into('>I',wad,68,zlib.crc32(wad))
    return wad,{'polygons':len(polys),'lines':len(lines),'points':len(points),'spawn_polygon':iy*nx+ix}

def convert(obj,bmp,out,width=48):
    if not 8<=width<=48: raise ValueError('Width must be between 8 and 48 world units')
    data=bmp.read_bytes()
    if len(data)<54 or data[:2]!=b'BM': raise ValueError('Colour map must be a BMP')
    iw,ih=struct.unpack_from('<ii',data,18)
    if not (1<=iw<=8192 and 1<=abs(ih)<=8192): raise ValueError('Unsupported BMP dimensions')
    verts,uvs,faces=read_obj(obj)
    x0=min(p[0] for p in verts);x1=max(p[0] for p in verts)
    z0=min(p[2] for p in verts);z1=max(p[2] for p in verts);base=min(p[1] for p in verts)
    if min(x1-x0,z1-z0)<=0: raise ValueError('Expected a Y-up terrain export')
    scale=width/max(x1-x0,z1-z0)
    vertices=[((x-(x0+x1)/2)*scale, -(z-(z0+z1)/2)*scale, (y-base)*scale+.75) for x,y,z in verts]
    actual_width=(x1-x0)*scale;height=(z1-z0)*scale
    if max(p[2] for p in vertices)>10: raise ValueError('Terrain too tall for the test level; reduce --width')
    # Deterministic location away from cell seams. Z is relative to flat floor=0.
    sx,sy=actual_width/24,height/24
    spawn=(round(sx*WU),round(sy*WU),math.ceil(height_at(vertices,faces,sx,sy)*WU)+8)
    wad,stats=make_map(actual_width,height,spawn)
    out.mkdir(parents=True,exist_ok=True)
    with (out/'terrain.obj').open('w') as f:
        f.write('# Sprintathon Z-up world-space terrain, converted from Myth2ools\n')
        for p in vertices: f.write('v '+' '.join(f'{v:.9f}' for v in p)+'\n')
        for uv in uvs:f.write('vt '+' '.join(f'{v:.9f}' for v in uv)+'\n')
        for face in faces:f.write('f '+' '.join(f'{v+1}/{t+1}' for v,t in face)+'\n')
    (out/'color.bmp').write_bytes(data)
    (out/'Myth Terrain.sceA').write_bytes(wad)
    stats.update(source_vertices=len(verts),source_triangles=len(faces),width=actual_width,height=height,
                 height_range=[min(v[2] for v in vertices),max(v[2] for v in vertices)],scale=scale,spawn=spawn,
                 texture=[iw,abs(ih)],axis_transform='(X,Y,Z) -> (X,-Z,Y)',terrain_only=True)
    (out/'import-info.json').write_text(json.dumps(stats,indent=2)+'\n')
    (out/'launch.sh').write_text('''#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 2 ]; then
    echo "Usage: bash launch.sh /path/to/sprintathon /path/to/Marathon-Infinity-data" >&2
    exit 2
fi
here=$(cd -- "$(dirname -- "$0")" && pwd)
export SPRINTATHON_TERRAIN_MESH="$here/terrain.obj"
export SPRINTATHON_TERRAIN_COLOR="$here/color.bmp"
export SPRINTATHON_TERRAIN_WORLD=1
exec "$1" "$2" "$here/Myth Terrain.sceA"
''')
    return stats

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('obj',type=pathlib.Path);parser.add_argument('bmp',type=pathlib.Path)
    parser.add_argument('output',type=pathlib.Path);parser.add_argument('--width',type=float,default=48)
    args=parser.parse_args()
    try:print(json.dumps(convert(args.obj,args.bmp,args.output,args.width),indent=2))
    except (ValueError,OSError,struct.error) as e:parser.exit(1,str(e)+'\n')
if __name__=='__main__': main()
