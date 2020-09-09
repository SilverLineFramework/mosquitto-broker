#!/usr/bin/env bash
docker login --username conixcenter
docker build . -t conixcenter/arena-broker
docker push conixcenter/arena-broker