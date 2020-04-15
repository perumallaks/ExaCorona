cat ../../run/exacorona-0.log | grep 'EVENTNAME.*TOTINFECTED' | sed 's/[][,]/ /g' | awk '{print $1 "," $4 "," $10}' > exacorona-0.csv
