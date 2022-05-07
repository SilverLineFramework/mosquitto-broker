#!/usr/bin/env bash
docker login --username slframework
docker build . -t slframework/arena-broker:$1
docker push slframework/arena-broker
