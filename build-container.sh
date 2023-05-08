#!/usr/bin/env bash

CNAME=${1:-"conixcenter/arena-broker"}   

docker login --username conixcenter
docker build . -t $CNAME
docker push $CNAME
