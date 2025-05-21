#!/bin/bash

set -e

if ! kind get clusters | grep -q '^kind$'; then
  echo "Kind cluster not found. Use kind create cluster"
  exit 1
fi

if ! kubectl cluster-info > /dev/null 2>&1; then
  echo "kubectl is not connected to any cluster."
  exit 1
fi

kind load docker-image localrepo/package-service:latest localrepo/vehicle-service:latest localrepo/customer-client:latest localrepo/vehicle-client:latest localrepo/manager-client:latest

echo "Deploying package service..."
kubectl apply -f package-service.yaml

echo "Deploying vehicle service..."
kubectl apply -f vehicle-service.yaml

echo "Deploying customer clients..."
kubectl apply -f customer-client.yaml

echo "Deploying vehicle clients..."
kubectl apply -f vehicle-client.yaml

echo "Deploying manager client..."
kubectl apply -f manager-client.yaml

echo "All components deployed successfully!"
