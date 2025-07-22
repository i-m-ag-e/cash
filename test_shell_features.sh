echo "Number of arguments to script: $#"
echo "\$0: $0"
! [ -z "$1" ] && echo "\$1: $1"
! [ -z "$2" ] && echo "\$2: $2"
! [ -z "$3" ] && echo "\$3: $3"
! [ -z "$4" ] && echo "\$4: $4"

echo ""

echo Hello || echo FAIL
echo "Double quoted" && echo 'Single quoted'
echo pipe test | tr a-z A-Z | grep PIPE
echo ""

[ $# -gt 0 ] && echo "Arg count: $#" || echo "No args"
echo "This script: $0" && echo "First: $1" && echo "Second: $2"
echo ""

echo START > out.txt && echo CONTINUE >> out.txt || echo ERROR > out.txt
cat < out.txt | grep START && grep CONTINUE < out.txt
echo ""

echo testing env && echo $PATH | grep /bin || echo missing PATH
echo ""

false && echo SHOULD_NOT_PRINT || echo "Exit status: $?"
echo ""

