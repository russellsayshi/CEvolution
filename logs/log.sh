#!/bin/bash
NUM=0
while true; do
  echo "========BEGIN: $NUM =======" >> log.txt;
  (time ../a.out) 2>> log.txt >> "log_$NUM.txt";
  echo -e "\n========END==============" >> log.txt;
  NUM=$((NUM+1))
done
