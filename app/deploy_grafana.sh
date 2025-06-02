helm repo add grafana https://grafana.github.io/helm-charts
helm repo update

helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
helm repo update

helm upgrade --install tempo grafana/tempo \
  --namespace tempo \
  --create-namespace

helm upgrade --install loki grafana/loki -f loki-values.yaml \
  --namespace loki \
  --create-namespace

helm upgrade --install prometheus prometheus-community/prometheus \
  --namespace prometheus \
  --create-namespace

helm upgrade --install grafana grafana/grafana \
    --namespace grafana \
    --create-namespace \
    --set adminPassword='admin' \
    --set service.type=LoadBalancer

kubectl rollout status deployment tempo -n tempo --timeout=120s || true
kubectl rollout status deployment grafana -n grafana --timeout=120s || true

kubectl get secret --namespace grafana grafana -o jsonpath="{.data.admin-password}" | base64 --decode

kubectl port-forward svc/grafana 3000:80 -n grafana

#ip tempo w grafanie - http://tempo.tempo.svc.cluster.local:3100
#ip lokiego w grafanie - http://loki.loki.svc.cluster.local:3100
#ip prometheusa w grafanie - http://prometheus-server.prometheus.svc.cluster.local:80
