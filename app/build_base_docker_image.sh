#!/bin/bash

LOCAL_REPO="localrepo"

DOCKERFILE="./Dockerfile.suu-base"
IMAGE_TAG="${LOCAL_REPO}/suu-base:latest"
echo "Building image $IMAGE_TAG using $DOCKERFILE ..."
docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" .
