#!/bin/bash

set -e

if ! kind get clusters | grep -q '^kind$'; then
  echo "Kind cluster not found. Use "kind create cluster --name kind""
  exit 1
fi

if ! kubectl cluster-info > /dev/null 2>&1; then
  echo "kubectl is not connected to any cluster."
  exit 1
fi

kind load docker-image localrepo/package-service:latest localrepo/vehicle-service:latest localrepo/customer-client:latest localrepo/vehicle-client:latest localrepo/manager-client:latest

echo "Deploying package service..."
kubectl apply -f package-service.yaml
kubectl rollout status deployment package-service -n package-service --timeout=120s || true

echo "Deploying vehicle service..."
kubectl apply -f vehicle-service.yaml
kubectl rollout status deployment vehicle-service -n vehicle-service --timeout=120s || true

echo "Deploying customer clients..."
kubectl apply -f customer-client.yaml

echo "Deploying vehicle clients..."
kubectl apply -f vehicle-client.yaml

echo "Deploying manager client..."
kubectl apply -f manager-client.yaml

echo "All components deployed successfully!"

echo "To list all pods, run: kubectl get pods"

echo "To see logs for a specific pod, run: kubectl logs <pod-name>"

echo "To access a pod's shell, run: kubectl exec -it <pod-name> -- /bin/sh"
