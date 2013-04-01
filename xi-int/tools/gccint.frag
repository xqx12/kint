: ${LOG:="cint.log"}

SRC=${@: -1}
BASE=`basename "$SRC"`
EXT=${BASE#*.}
BASE=`basename $BASE .c`

COMP=`case "$@" in *" -c "*) echo y ;; esac`
if [ "$EXT" == "c" -a "$COMP" == "y" ]; then
	echo ============================ >> $LOG
	echo $CINT $CINTARGS $@ >> $LOG
	$CINT $CINTARGS $@ >> $LOG 2>&1
	printf "  [CINT]  %s ... " "$obj/$BASE.bc"
	if [ $? -eq 0 -a -f "$BASE.bc" ]; then
		echo -e "\033[0;40;32mOK.\033[0m"
		mv $BASE.bc $obj/$BASE.bc
	else
		echo -e "\033[0;40;31mFailed!\033[0m"
	fi
fi

gcc $@
