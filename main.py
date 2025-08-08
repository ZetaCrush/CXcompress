from collections import Counter,defaultdict
import json
import random
from tqdm import tqdm
import zstandard as zstd

def compress(s):
    d={w:i for i,w in enumerate(open("dict").read().split("\n"))}
    l=len(d)
    words=s.split(" ")
    g0=set()
    for i in range(1,len(words)):
        w0=words[i-1]
        w1=words[i]
        if w0 in d and w1 in d:
            p0=d[w0]
            p1=d[w1]
            if p1>p0:
                diff=p1-p0
            else:
                diff=l+p1-p0
            g0.add(diff)
    g1=set()
    for i in range(2,len(words)):
        w0=words[i-2]
        w1=words[i]
        if w0 in d and w1 in d:
            p0=d[w0]
            p1=d[w1]
            if p1>p0:
                diff=p1-p0
            else:
                diff=l+p1-p0
            g1.add(diff)

    x0=[]
    last=0
    for i in range(l):
        if i not in g0:
            x0.append(chr(i-last))
            last=i
    x0="".join(x0)
    x1=[]
    last=0
    for i in range(l):
        if i not in g1:
            x1.append(chr(i-last))
            last=i
    x1="".join(x1)
    x=x0+x1
    p0={}
    p1={}
    t0,t1=0,0
    for i,k in enumerate(d.items()):
        p0[k]=i-t0
        p1[k]=i-t1
        if i in g0:
            t0+=1
        if i in g1:
            t1+=1
    y=[]
    z=[]
    for i in range(2,len(words)):
        w0=words[i-2]
        w1=words[i-1]
        w2=words[i]
        if w2 in d:
            if w0 in p1 and w1 in p0:
                z.append(chr(p0[w1]))
            elif w1 in p0:
                z.append(chr(p0[w1]))
            elif w0 in p1:
                z.append(chr(p1[w0]))
        else:
            y.append(w2)
    y=" ".join(y)
    z="".join(z)
    x=x+y+z
    return zstd.compress(x.encode("utf-8","replace"),level=22)

if __name__=="__main__":
    s=open("dickens",encoding="latin-1").read()[:]
    print(len(s))
    #print(len(zstd.compress(s.encode("utf-8","replace"),level=22)))
    c=compress(s)
    print(len(c))
