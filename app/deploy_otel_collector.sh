#!/bin/bash

set -e

kubectl apply -f https://github.com/cert-manager/cert-manager/releases/download/v1.17.2/cert-manager.yaml
echo "> Deployed cert-manager"

echo "> Waiting for cert-manager to start"
kubectl rollout status deployment cert-manager -n cert-manager --timeout=120s || true

kubectl apply -f https://github.com/open-telemetry/opentelemetry-operator/releases/latest/download/opentelemetry-operator.yaml
echo "> Deployed OpenTelemetry Operator"

echo "> Waiting for OpenTelemetry Operator to start"
kubectl rollout status deployment opentelemetry-operator-controller-manager -n opentelemetry-operator-system --timeout=120s || true

kubectl apply -f otel-collector.yaml
echo "> Deployed OpenTelemetry Collector"
