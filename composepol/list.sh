#!/bin/bash
cat $1 | jq '.' | grep "\"name\":" | cut -d ":" -f 2 | tr -d "\", "
