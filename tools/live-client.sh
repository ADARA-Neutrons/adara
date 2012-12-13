#!/bin/bash

host=localhost
port=31415

[[ $1 ]] && host=$1
[[ $2 ]] && port=$2

pkt="\x04\x00\x00\x00"
pkt="$pkt\x00\x06\x40\x00"
pkt="$pkt\x00\x00\x00\x00"
pkt="$pkt\x00\x00\x00\x00"
pkt="$pkt\x01\x00\x00\x00"
( echo -en "$pkt"; while true; do sleep 3600; done) | nc -6 $host $port 
