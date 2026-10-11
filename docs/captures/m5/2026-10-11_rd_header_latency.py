import sys
def load(p):
    t=[];v=[]
    for l in open(p):
        s=l.strip()
        if not s or s[0] in '#t': continue
        a,b=s.split(',')[:2]; t.append(float(a)); v.append(float(b))
    return t,v
t3,v3=load('lat_ch3.csv'); t1,v1=load('lat_ch1.csv')
gate=[t1[i] for i in range(1,len(v1)) if v1[i-1]>=2.5>v1[i]][0]
th=(max(v3)+min(v3))/2
pulses=[t3[i] for i in range(1,len(v3)) if v3[i-1]<th<=v3[i] and t3[i]<gate]
CELL=1e6/489600*1e-6
# bits with the time each 1-bit arrives
bits=[]
for a,b in zip(pulses,pulses[1:]):
    n=max(1,round((b-a)/CELL))
    bits += [(0,None)]*(n-1)+[(1,b)]
nibs=[]; reg=0; nb=0; first=None
for i,(bit,tm) in enumerate(bits):
    if nb==0 and not bit: continue
    reg=(reg<<1)|bit; nb+=1
    if nb==8:
        # nibble complete at the time of the NEXT 1-bit (decoder sees it there)
        nxt=next((x for (b2,x) in bits[i+1:] if b2),None)
        nibs.append((reg,nxt)); reg=0; nb=0
seq=[n for n,_ in nibs]
print('threshold %.2f V, %d pulses, gate fall at %.1f us' % (th,len(pulses),gate*1e6))
print('last nibbles:', ' '.join('%02X'%n for n in seq[-24:]))
for i in range(len(seq)-1,0,-1):
    if seq[i-1]==0xDE and seq[i]==0xAA:
        tdone=nibs[i][1]
        print('DE AA at nibble %d; decoder sees it at %.2f us; gate falls %.2f us later' % (i, tdone*1e6, (gate-tdone)*1e6))
        break
