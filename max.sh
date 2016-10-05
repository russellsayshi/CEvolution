#!/bin/bash
MAX=0
LINE=0
INC=0
for N in $(cat log.txt | grep real | cut -d 'm' -f 2 | cut -d '.' -f 1); do
  INC=$((INC+1))
  if [ $N -gt $MAX ]; then
    MAX=$N
    LINE=$INC
  fi
done
echo $MAX
echo "Line: $LINE"
