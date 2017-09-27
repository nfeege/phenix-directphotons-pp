#!/bin/bash
# Function: Combine root files ten by ten.

cd "histos"
rm -f total.root tmp.root

for prename in "AnaFastMC-PH-nowarn-" "AnaFastMC-PH-warn-" "AnaFastMC-Fast-nowarn-" "AnaFastMC-Fast-warn-" ; do
    files=""
    count=0

    for FILE in ${prename}*.root ; do
	files="${files} ${FILE}"
	(( count++ ))
	if (( "${count}" > "9" )) ; then
	    if [[ -f "total.root" ]] ; then
		hadd tmp.root total.root ${files}
	    else
		hadd tmp.root ${files}
	    fi
	    mv -f tmp.root total.root
	    files=""
	    count=0
	fi
    done

    if [[ -n "${files}" ]] ; then
	if [[ -f "total.root" ]] ; then
	    hadd tmp.root total.root ${files}
	else
	    hadd tmp.root ${files}
	fi
	mv -f tmp.root total.root
    fi

    mv total.root ../${prename}histo.root
done