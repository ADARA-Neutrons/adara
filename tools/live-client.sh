#!/bin/bash

request=1
host=localhost
port=31415

[[ $1 == "-t" ]] && {
	[[ $2 ]] || {
		echo "Missing timestamp" 1>&2
		exit 2
	}

	request=$2
	shift 2
}

[[ $1 ]] && host=$1
[[ $2 ]] && port=$2

rbyte0=$(printf "\\\\x%02x" $(( request % 256 )) )
(( request /= 256 ))
rbyte1=$(printf "\\\\x%02x" $(( request % 256 )) )
(( request /= 256 ))
rbyte2=$(printf "\\\\x%02x" $(( request % 256 )) )
(( request /= 256 ))
rbyte3=$(printf "\\\\x%02x" $(( request % 256 )) )
(( request /= 256 ))

pkt="\x04\x00\x00\x00"
pkt="$pkt\x00\x06\x40\x00"
pkt="$pkt\x00\x00\x00\x00"
pkt="$pkt\x00\x00\x00\x00"
pkt="$pkt$rbyte0$rbyte1$rbyte2$rbyte3"

( echo -en "$pkt"; while true; do sleep 3600; done) | nc -6 $host $port 
