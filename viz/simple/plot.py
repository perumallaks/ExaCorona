import matplotlib.pyplot as plt
import numpy as np
import csv

alls = [] #series ID
allx = [] #timestamps
ally = [] #total infections

with open('exacorona-0.csv','r') as csvfile:
    plots = csv.reader(csvfile, delimiter=',')
    for row in plots:
        sid = int(row[0])
        ts = float(row[1])
        totinf = float(row[2])
        alls.append(sid)
        allx.append(ts)
        ally.append(totinf)

fig, ax = plt.subplots()

minsid = min(alls)
maxsid = max(alls)
for sid in range(minsid,maxsid+1):
    x = []
    y = []
    for i in range(0,len(allx)):
        if( alls[i] == sid ):
            x.append(allx[i])
            y.append(ally[i])
    ax.plot(x, y, label='Location '+str(sid))

plt.legend()
ax.set(xlabel='time (s)', ylabel='Total #infections',
       title='ExaCorona')

fig.savefig("exacorona.png")
plt.show()
