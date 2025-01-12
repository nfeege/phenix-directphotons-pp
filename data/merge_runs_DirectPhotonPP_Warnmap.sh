#! /bin/zsh

DATASETS=(
#    Run9pp200MinBias
#    Run9pp200ERT
#    Run9pp500MinBias
#    Run9pp500ERT
    Run13pp510MinBias
    Run13pp510ERT
)

INPUT_DIR_BASE="/phenix/spin3/nfeege/taxi/"
OUTPUT_DIR="./data/"

RUNLISTFILE="../runqa/Run13pp510_RunQuality.txt"

IFS=$'\n' RUNLIST=($(cat $RUNLISTFILE | grep 0$ | cut -d\  -f1))
NRUNS=$(cat $RUNLISTFILE | grep 0$ | wc -l)

echo "Using Run List $RUNLISTFILE"

TEMPFILE=templist.txt

for DATASET in $DATASETS; do

    echo "Processing data set $DATASET ..."

    if [[ -e $TEMPFILE ]]; then
	rm $TEMPFILE
	touch $TEMPFILE
    fi

    OUTPUT_FILE="${OUTPUT_DIR}/WarnmapData_${DATASET}.root"

    INPUT_DIR="$INPUT_DIR_BASE/$DATASET"

    [[ ! -d $INPUT_DIR ]] && echo "ERROR: Directory $INPUT_DIR does not exist. EXIT." && exit

    TAXI_DATA_DIR=`awk '$2=="Run_DirectPhotonPP_Warnmap" {print $1}' $INPUT_DIR/taxilist.txt`

    INPUT_DIR="$INPUT_DIR/$TAXI_DATA_DIR/data/"

    echo "Merging files from input directory $INPUT_DIR to $OUTPUT_FILE ... "

    ## LIMIT hadd to 1000 files at a time because of "Too many open files" error
    if [[ $NRUNS -le 1000 ]]; then

	for RUN in $RUNLIST; do
	    echo "$INPUT_DIR/WarnmapData-${RUN}.root" >> $TEMPFILE
	done

	FILELIST=("${(@f)$(cat $TEMPFILE)}")
	echo $FILELIST
	hadd $OUTPUT_FILE $FILELIST
    else
	echo "More than 1000 runs, stop processing. ERROR."
	exit
    fi

#    ## LIMIT hadd to 1000 files at a time because of "Too many open files" error
#    NFILES=$( ls $INPUT_DIR/WarnmapData*.root | wc -l )
#    echo "Number of files to merge: $NFILES"
#
#    if [[ $NFILES -le 1000 ]]; then
#
#	FILELIST=("${(@f)$(ls $INPUT_DIR/WarnmapData*.root)}")
#
##        haddPhenix $OUTPUT_FILE $FILELIST
#	hadd $OUTPUT_FILE $FILELIST
#
#    elif [[ $NFILES -le 2000 ]]; then
#
#	echo "File list between 1000 and 2000 entries long, need to split..."
#
#	N_SUB1=1000;
#	let "N_SUB2 = $NFILES - $N_SUB1"
#
#	echo $NFILES
#	echo $N_SUB1
#	echo $N_SUB2
#
#	FILELIST_SUB1=("${(@f)$(ls $INPUT_DIR/WarnmapData*.root | head -${N_SUB1})}")
#	FILELIST_SUB2=("${(@f)$(ls $INPUT_DIR/WarnmapData*.root | tail -${N_SUB2})}")
#
#	OUTPUT_FILE_SUB1="${OUTPUT_DIR}/WarnmapData_${DATASET}_Temporary_Sub1.root"
#	OUTPUT_FILE_SUB2="${OUTPUT_DIR}/WarnmapData_${DATASET}_Temporary_Sub2.root"
#
#	hadd $OUTPUT_FILE_SUB1 $FILELIST_SUB1
#	hadd $OUTPUT_FILE_SUB2 $FILELIST_SUB2
#
#	hadd $OUTPUT_FILE $OUTPUT_FILE_SUB1 $OUTPUT_FILE_SUB2
#
#	rm $OUTPUT_FILE_SUB1 $OUTPUT_FILE_SUB2
#
#    else
#
#	echo "File list longer than 2000 entries, stop processing here."
#	return
#
#    fi

    echo "DONE."

done

exit
