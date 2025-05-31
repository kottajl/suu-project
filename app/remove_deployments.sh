#!/bin/bash

kubectl delete deployments --all --all-namespaces
kubectl delete daemonsets --all --all-namespaces
kubectl delete statefulsets --all --all-namespaces