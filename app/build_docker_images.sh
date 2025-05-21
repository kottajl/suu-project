#!/bin/bash
set -e

LOCAL_REPO="localrepo"

IMAGES=(
  "package-service"
  "vehicle-service"
  "customer-client"
  "manager-client"
  "vehicle-client"
)

echo "Starting building Docker images"

for IMAGE in "${IMAGES[@]}"; do
  DOCKERFILE="./Dockerfile.${IMAGE}"
  if [ ! -f "$DOCKERFILE" ]; then
    echo "Warning: $DOCKERFILE not found"
    continue
  fi

  IMAGE_TAG="${LOCAL_REPO}/${IMAGE}:latest"
  echo "Building image $IMAGE_TAG using $DOCKERFILE ..."
  docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" .
done

echo "All images built"
