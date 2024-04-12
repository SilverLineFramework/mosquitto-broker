#!/usr/bin/env bash

docker buildx create --name mybuilder --use --bootstrap

DOCKER_USER=arenaxrorg
docker login --username $DOCKER_USER
docker buildx build . --no-cache --push --platform linux/amd64 -t $DOCKER_USER/arena-broker:$1 -t $DOCKER_USER/arena-broker:latest
