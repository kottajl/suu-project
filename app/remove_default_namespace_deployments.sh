#!/bin/bash

kubectl delete deployments --all -n default
kubectl delete daemonsets --all -n default
kubectl delete statefulsets --all -n default
