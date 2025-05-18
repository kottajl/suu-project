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

echo "Starting build and load for Kind cluster..."

for IMAGE in "${IMAGES[@]}"; do
  DOCKERFILE="Dockerfile.${IMAGE}"
  if [ ! -f "$DOCKERFILE" ]; then
    echo "Warning: $DOCKERFILE not found, building with default Dockerfile"
    DOCKERFILE="Dockerfile"
  fi

  IMAGE_TAG="${LOCAL_REPO}/${IMAGE}:latest"
  echo "Building image $IMAGE_TAG using $DOCKERFILE ..."
  docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" .

  echo "Loading $IMAGE_TAG into Kind cluster..."
  kind load docker-image "$IMAGE_TAG"
done

echo "All images built and loaded into Kind."