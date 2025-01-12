import csv
import numpy as np

mapfiles0 = [
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins3to4_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins5to6_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins7to9_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins10to12_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins13to25_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins3to25_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins3to4_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins5to6_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins7to9_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins10to12_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins13to25_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins3to25_nsigma10_niter10.txt',
    'warnmap-output/Deadmap_Run13pp510MinBias_ybins3to25.txt'
    ]


mapfiles1 = [
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins3to4_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins5to6_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins7to9_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins10to12_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins13to25_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510MinBias_ybins3to25_nsigma10_niter10.txt'
    ]


mapfiles2 = [
    'warnmap-output/Warnmap_Run13pp510ERT_ybins3to4_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins5to6_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins7to9_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins10to12_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins13to25_nsigma10_niter10.txt' ,
    'warnmap-output/Warnmap_Run13pp510ERT_ybins3to25_nsigma10_niter10.txt'
    ]


mapfiles3 = [
    'warnmap-final/Warnmap_Run13pp510MinBias_Final.txt' ,
    'warnmap-final/Warnmap_Run13pp510ERT_Final.txt'
]

# select set of files to merge
mapfiles=mapfiles0

# array of total towers per sector
ntower_total = [
    2592 ,
    2592 ,
    2592 ,
    2592 ,
    2592 ,
    2592 ,
    4608 ,
    4608
    ]

# loop over all mapfiles and load maps into arrays
warnmaps = list()

for mapfile in mapfiles:

    warnmap = np.loadtxt( mapfile, delimiter=' ' ).astype(np.int32)
    warnmaps.append( warnmap )


# merge warnmap files
print "*** Number of hot channels per sector for merged warnmap ***"

warnmap_merged = warnmaps[0].copy()

for warnmap in warnmaps:
    warnmap_merged = np.maximum( warnmap_merged, warnmap )

# count hot channels
for sector in np.arange(0,8):
    nhot = ( ( warnmap_merged[:,0] == sector ) & ( warnmap_merged[:,3] == 50 ) ).sum()
    frachot = float(nhot) / float(ntower_total[sector])

    ndead = ( ( warnmap_merged[:,0] == sector ) & ( warnmap_merged[:,3] == 100 ) ).sum()
    fracdead = float(ndead) / float(ntower_total[sector])

    print 'sector: %i hot  fraction: %.2f  total:  %i' % ( sector, frachot,  nhot  )
    print 'sector: %i dead fraction: %.2f  total:  %i' % ( sector, fracdead, ndead )

nlive_detector=0
ntotal_detector=0
for sector in np.arange(0,8):
    nlive = ( ( warnmap_merged[:,0] == sector ) & ( warnmap_merged[:,3] < 20 ) ).sum()
    fraclive = float(nlive) / float(ntower_total[sector])

    nlive_detector+=nlive
    ntotal_detector+=ntower_total[sector]

    print 'sector: %i live fraction: %.2f  total:  %i' % ( sector, fraclive, nlive )

print 'Calorimeter live fraction: %.3f' % ( float(nlive_detector) / float(ntotal_detector) )

# save warnmap to txt file
np.savetxt( 'warnmap-final/warnmap_merged_python.txt', warnmap_merged, delimiter=' ', fmt='%1.1d' )

