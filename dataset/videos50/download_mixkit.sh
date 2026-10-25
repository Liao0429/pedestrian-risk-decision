#!/bin/bash
# Download Mixkit free stock pedestrian/crowd videos (360p) into videos50/
cd "$(dirname "$0")"
IDFILE="/h/PedestrianRiskDecision/mixkit_good_ids_lf.txt"
LOG="download_mixkit.log"
: > "$LOG"

downloaded=0
skipped=0
failed=0

while IFS= read -r id; do
  [ -z "$id" ] && continue
  out="mixkit_${id}.mp4"
  if [ -f "$out" ] && [ -s "$out" ]; then
    skipped=$((skipped+1))
    continue
  fi
  ok=0
  for attempt in 1 2 3 4 5; do
    code=$(curl -s -x http://127.0.0.1:7890 --max-time 60 -A "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120.0" \
      "https://assets.mixkit.co/videos/$id/$id-360.mp4" -o "$out" -w "%{http_code}")
    if [ "$code" = "200" ] && [ -s "$out" ]; then
      ok=1
      break
    else
      echo "$(date +%H:%M:%S) id=$id attempt=$attempt code=$code retrying" >> "$LOG"
      rm -f "$out"
      sleep 2
    fi
  done
  if [ "$ok" = "1" ]; then
    downloaded=$((downloaded+1))
    echo "$(date +%H:%M:%S) OK id=$id" >> "$LOG"
  else
    failed=$((failed+1))
    echo "$(date +%H:%M:%S) FAIL id=$id" >> "$LOG"
  fi
  sleep 0.5
done < "$IDFILE"

echo "DONE downloaded=$downloaded skipped=$skipped failed=$failed total_files=$(ls -1 mixkit_*.mp4 2>/dev/null | wc -l)" >> "$LOG"
echo "DONE downloaded=$downloaded skipped=$skipped failed=$failed total_files=$(ls -1 mixkit_*.mp4 2>/dev/null | wc -l)"