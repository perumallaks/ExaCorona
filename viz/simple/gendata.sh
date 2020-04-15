cat ../../run/exacorona.log | grep 'EVENTNAME.*TOTINFECTED' | sed 's/[][,]/ /g' | awk '{print $1 "," $4 "," $10}' > exacorona.csv
