pre=$1
n=$2

for i in $(seq 1 $n); do
    if [ "$(ps -o pgid= $$ | tr -d ' ')" = "$(ps -o tpgid= $$ | tr -d ' ')" ]; then
        echo "hello $i from $pre" 
    else
        echo "hello $i from $pre" >> background.log
    fi
    sleep 1
done
