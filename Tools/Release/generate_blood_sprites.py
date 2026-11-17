import math, random, struct
from pathlib import Path

def blood_sprite(kind, seed):
    rng=random.Random(seed)
    size=128
    if kind=='pool':
        blobs=[(0,0,.64,.46,1)] + [(rng.uniform(-.44,.44),rng.uniform(-.26,.26),rng.uniform(.12,.31),rng.uniform(.10,.24),.70) for _ in range(9)]
    elif kind=='mist':
        blobs=[(0,0,.74,.68,.66)]
    else:
        blobs=[(0,0,.35,.26,.95)] + [(rng.uniform(-.74,.74),rng.uniform(-.74,.74),rng.uniform(.025,.15),rng.uniform(.02,.12),rng.uniform(.6,1)) for _ in range(32)]
    pixels=bytearray()
    for row in range(size):
        y=2*(row+.5)/size-1
        for col in range(size):
            x=2*(col+.5)/size-1
            grain=.95+.04*math.sin(x*58+y*31)+.015*math.sin(x*121-y*103)
            a=0
            for bx,by,rx,ry,strength in blobs:
                distance=((x-bx)/rx)**2+((y-by)/ry)**2
                fade=(1-distance)*(1 if kind=='mist' else 3.5)
                a=max(a,strength*min(1,max(0,fade))*grain)
            pixels.extend((5,8,int(47+70*a) if kind=='pool' else int(70+60*a),int(min(a,1)*255)))
    return struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,size,size,32,40)+pixels

SPRITES=(('g_blood_spray', 'splat'), ('g_blood_mist', 'mist'), ('g_splat1', 'splat'), ('g_splat2', 'splat'), ('g_splat3', 'splat'), ('g_wall_splat', 'splat'), ('g_pool1', 'pool'), ('g_pool2', 'pool'))

if __name__ == "__main__":
    for i, (name, kind) in enumerate(SPRITES):
        Path(name+".tga").write_bytes(blood_sprite(kind,5211+i))
