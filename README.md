# gRPC w C++ z użyciem OTel

## Wysokowydajny, uniwersalny framework RPC typu open source

## Autorzy (rok 2025, grupa 9:45 czwartek)
- Patryk Czuchnowski
- Michał Pędrak
- Andrzej Wacławik
- Ivan Zarzhitski

## Wprowadzenie

W ramach projektu, budujemy prostą aplikację typu klient-serwer opartą o [gRPC](https://www.cncf.io/projects/grpc/) z włączoną instrumentacją danych telemetrycznych (śladów, metryk, logów prowadzonej komunikacji) przy wykorzystaniu [OpenTelemetry](https://www.cncf.io/projects/opentelemetry/). Dzięki wykorzystaniu OTLP (czyli protokołu OpenTelemetry), zebrane dane będą mogły być eksportowane do narzędzi wizualizacyjnych takich jak [Grafana](https://grafana.com/).

Celem projektu jest stworzenie aplikacji, która nie tylko umożliwia komunikację pomiędzy klientem a serwerem za pomocą gRPC, ale również zapewnia pełną obserwowalność działania systemu, czyli zdolność do zrozumienia, co dzieje się wewnątrz niego, na podstawie zewnętrznych sygnałów (danych telemetrycznych). Dzięki integracji z OpenTelemetry, aplikacja będzie monitorowana pod kątem wydajności oraz dostępności, co pozwoli na szybsze wykrywanie ewentualnych problemów.

## Podstawy teoretyczne i stos technologiczny

Framework gRPC to nowoczesny i wydajny system typu open source do zdalnego wywoływania procedur, który może działać w niemalże dowolnym środowisku. Umożliwia efektywne łączenie usług zarówno wewnątrz, jak i pomiędzy centrami danych, oferując możliwość podłączania modułów do obsługi równoważenia obciążenia, śledzenia danych telemetrycznych czy uwierzytelniania. 

OpenTelemetry to standard i zestaw narzędzi typu open source, służący do zbierania, przetwarzania i eksportowania danych telemetrycznych z aplikacji, takich jak metryki, logi, ślady i profile. Jest on rozwijany przez Cloud Native Computing Foundation (CNCF) i ma na celu ujednolicenie sposobu obserwowalności systemów rozproszonych.

OTLP (OpenTelemetry Protocol) to standaryzowany protokół komunikacyjny używany przez OpenTelemetry do przesyłania danych telemetrycznych między aplikacjami do obserwowalności (takimi jak Grafana, Jaeger, Prometheus). Jest on binarnym protokołem opartym na gRPC lub [HTTP/Protobuf](https://protobuf.dev/).

W ramach projektu, wykorzystujemy technologię gRPC do komunikacji, OpenTelemetry do zbierania danych telemetrycznych. Protokół OTLP jest używany do eksportowania zebranych danych telemetrycznych do narzędzia Grafana, służącego do wizualizacji wyników. Projekt zostanie skonteneryzowany za pomocą Kubernetes oraz zdeployowany na AWS.

## Opis koncepcji

W ramach projektu stworzymy system zarządzania flotą pojazdów i przesyłkami w architekturze rozproszonej. Kluczową rolę odgrywa w nim komunikacja między niezależnymi usługami oraz monitorowanie stanu systemu i jego komponentów w czasie rzeczywistym.

## Architektura rozwiązania

![Diagram architektury](./images/architecture_diagram.png)

Architektura systemu została przedstawiona na powyższym diagramie. System składa się z następujących komponentów:

### **Vehicle Service** (gRPC serwer) 
Odpowiada za zarządzanie lokalizacją pojazdów.

-   **sendLocation()** – Client-Streaming  
Wywoływana przez pojazdy, pojazd wysyła strumień danych z lokalizacją do serwera.

-   **trackVehicle()** – Server-Streaming  
Menedżer otrzymuje ciągły strumień lokalizacji wskazanego pojazdu.

-   **getPackagesDeliveredBy()** – Unary  
Zwraca ile paczek zostało dostarczonych przez podany pojazd w danym dniu.

### **Package Service (gRPC serwer)** 
Zarządza paczkami i udostępnia informacje o nich klientom.

-   **updatePackages()** – Bidirectional-Streaming  
Dwukierunkowa komunikacja z pojazdem, pojazd wysyła aktualizacje statusu przesyłek, serwer odpowiada jaką paczkę dostarczyć następnie i gdzie.

-   **createPackage()** – Unary  
Wywoływana przez klienta, tworzy nową paczkę do dostarczenia – z adresem nadawcy, odbiorcy itp.

-   **getPackageStatus()** – Unary  
Wywoływana przez klienta, zwraca aktualny status paczki.

### **Pojazdy (gRPC klienci)** 
Wysyłają dane lokalizacyjne i aktualizują informacje o dostawach.

### **Menedżer (gRPC klient)** 
Może śledzić lokalizację wybranego pojazdu oraz w celu monitorowania wydajności wysyła zapytania do serwisu ile dany pojazd dostarczył paczek.

### **Klienci (gRPC klienci)** 
Tworzą i śledzą przesyłki.

### **Dane telemetryczne**
Dzięki OpenTelemetry, komponenty gromadzą dane, które są następnie przesyłane przez OTLP w celu eksportu do narzędzia Grafana, która umożliwia wizualizację i monitorowanie aplikacji w czasie rzeczywistym. 

## Opis konfiguracji środowiska

Projekt został przygotowany w języku **C++** z wykorzystaniem frameworka **gRPC** oraz instrumentacji **OpenTelemetry**. Do jego uruchomienia wymagane są:

- **Docker** w wersji 20.10 lub nowszej,
- **g++** i **make** (opcjonalnie – tylko w przypadku lokalnej kompilacji poza Dockerem),
- Narzędzia do wizualizacji danych telemetrycznych, np. **Grafana** z **Tempo** i **Prometheus** (opcjonalnie).
- **kind** do lokalnego uruchomienia aplikacji na klastrze Kubernetes

### Struktura środowiska:

- Każdy komponent (np. `vehicle-service`, `customer-client`) posiada własny plik `Dockerfile`.
- Pliki `.yaml` (`vehicle-client.yaml`, `package-service.yaml` itd.) zawierają definicje uruchomieniowe (dla Kubernetesa).
- Skrypty `build_base_docker_image.sh` oraz `build_docker_images.sh` służą do automatycznej budowy obrazów Dockerowych.
- Skrypt `deploy_kind.sh` służy do automatycznego deploymentu do lokalnego klastra `kind`
- Katalog `src/` zawiera kod źródłowy w C++ oraz pliki `.proto` definiujące interfejsy gRPC.

## Metoda instalacji

Instalacja projektu polega na budowie obrazów Dockerowych i ich uruchomieniu. Poniżej przedstawiono kroki wymagane do przygotowania środowiska:

### Krok 1: Budowa środowiska i import niezbędnych bibliotek

```bash
cd app
./build_base_docker_image.sh
```
### Krok 2: Budowa wszystkich obrazów komponentów
```bash
./build_docker_images.sh
```

Ten skrypt tworzy obrazy dla:
- `vehicle-service`
- `vehicle-client`
- `package-service`
- `manager-client`
- `customer-client`

### Krok 3: (Opcjonalnie) Budowa pojedynczego obrazu
Można zbudować pojedynczą usługę ręcznie:
```bash
docker build -f Dockerfile.vehicle-service -t suu/vehicle-service .
```

### Krok 4: Uruchomienie usług
Można uruchomić kontenery ręcznie lub wykorzystać pliki `.yaml` i środowisko Kubernetes (np. `kind`, `minikube` lub chmurę).

Przykład użycia `kubectl`:
```bash
kubectl apply -f vehicle-service.yaml
```
Lub uruchomienie w trybie developerskim przy użyciu `docker run`.

Stworzyliśmy również skrypt `deploy_kind.sh` który automatycznie deployuje obrazy Dockerowe do klastra `kind`.

```bash
./deploy_kind.sh
```

## Uruchamianie projektu – krok po kroku

1. **Wymagania wstępne**
   - Zainstalowane narzędzia:
     - Docker i Kind (Kubernetes in Docker)
     - `kubectl`
     - `helm`
     - `make`
   - Otwarty dostęp do portów 3000 (Grafana), 9090 (Prometheus)

2. **Budowanie obrazów Dockera**
   - Zbuduj obraz bazowy oraz obrazy aplikacji.

3. **Uruchomienie klastra i komponentów**
   - Uruchom lokalny klaster Kind.
   - Zainstaluj Prometheus i Loki za pomocą Helm Charts.
   - Zainstaluj Grafanę do wizualizacji metryk i logów.
   - Uruchom kolektor OpenTelemetry.

4. **Wdrożenie mikroserwisów**
   - Zastosuj manifesty Kubernetes dla każdego mikroserwisu przez bash skrypt:
 ```bash
./deploy_kind.sh
```
   - Stwórz Otel collector, który będzie zbierać dane i eksportować ich do Grafany.
```bash
./deploy_otel_collector.sh
```

   - (Opcjonalne) Skrypty dla usuwanie deploymentów:
```bash
./remove_deployments.sh
./remove_default_namespace_deployments.sh
```

6. **Weryfikacja wdrożenia**
   - Użyj `kubectl` do sprawdzenia stanu podów i logów.
 ```bash
./kubectl get pods
./kubectl get pods --namespace loki
./kubectl get pods --namespace prometheus
./kubectl get pods --namespace tempo
```
   - Zaloguj się do interfejsu Grafana i dodaj źródła danych (Prometheus, Loki).
```bash
./deploy_grafana.sh

```
   - Grafana będzie dostępna w przeglądarce pod adresem `localhost:3000`
---

## Podejście Infrastructure as Code

Projekt wykorzystuje podejście *Infrastructure as Code (IaC)* poprzez:

- Skrypty automatyzujące budowę i wdrożenie aplikacji (`*.sh`)
- Pliki manifestów Kubernetes (`*.yaml`) definiujące wszystkie komponenty systemu
- Wykorzystanie Helm Charts do zarządzania Prometheusem, Lokim i Grafaną
- Deklaratywne i modułowe zarządzanie usługami w architekturze mikroserwisowej

## Etapy uruchomienia demonstracyjnego

Poniżej jest przedstawiony pełny spis komend bash dla uruchomienia demonstracyjnego dla czystego środowiska
```bash
./build_base_docker_image.sh
./build_docker_images.sh
kind create cluster
./deploy_kind.sh
./deploy_otel_collector.sh
./deploy_grafana.sh
```

### Konfiguracja środowiska testowego

   - Upewnij się, że wszystkie pody są w stanie `Running`.
   - Zweryfikuj, że Prometheus i Grafana mają dostęp do odpowiednich endpointów.
     
### Przygotowanie danych testowych

   - Przesyłki, pojazdy i klienty są generowane automatyczne dla 3 pojazdów i 3 klientów.
   - Dane generowane przez mikroserwisy będą rejestrowane w systemie monitoringu i logowania.

### Uruchomienie aplikacji

   - Aplikacja działa w całości w klastrze Kind Kubernetes.
   - Każdy mikroserwis wykonuje swoje zadania automatycznie po wdrożeniu i może być testowany za pomocą logów i metryk.

### Prezentacja wyników działania
Żeby zobaczyć wyniki jest potrzebne dodanie źródeł dannych dla serwisów:
- Tempo: `http://tempo.tempo.svc.cluster.local:3100`
- Loki: `http://loki.loki.svc.cluster.local:3100`
- Prometheus: ` http://prometheus-server.prometheus.svc.cluster.local:80`
  
Wyniki prezentowane w formacie trace, metryk i logów w Grafanie:
![Tempo traces](https://github.com/user-attachments/assets/8f1d85e1-8e7d-4c73-a096-360da14a790d)
![Metrics](https://github.com/user-attachments/assets/255fb8b9-2ed1-4803-827b-d2bfe6938627)
![Loki logs](https://github.com/user-attachments/assets/71f66da1-bd6f-4112-a481-c4490e9bcdb2)

## Wykorzystanie AI w projekcie

(IZ) AI został używany w projekcie głównie w dwóch celach - ulepszenie dokumentacji i debugowanie logów.
```
Find what is wrong with this log.
<For example log from vechicle-service with some kind of error>
```

## Podsumowanie i wnioski

Projekt gRPC-C++-OTel stanowi kompletny przykład systemu mikroserwisowego, który integruje:
- konteneryzację (Docker),
- zarządzanie cyklem życia usług (Kubernetes),
- obserwowalność (OpenTelemetry, Prometheus, Grafana, Loki),
- oraz podejście Infrastructure as Code (IaC).

Dzięki modularnej strukturze i wykorzystaniu otwartych standardów projekt:
- jest łatwy do wdrożenia i rozwijania,
- umożliwia szybką diagnostykę działania systemu za pomocą metryk i logów,
- pozwala na integrację z narzędziami AI do analizy danych runtime.

**Wnioski:**
- Zastosowanie mikroserwisów znacząco ułatwia skalowanie i niezależny rozwój komponentów.
- Monitoring i logging od początku projektu zwiększa jego niezawodność i skraca czas diagnostyki błędów.
- Podejście IaC zapewnia powtarzalność wdrożeń i minimalizuje błędy ludzkie.
- Projekt może stanowić solidną bazę do dalszego rozwoju – np. w kierunku automatycznego skalowania, zaawansowanej analityki czy wykorzystania AI do automatycznej reakcji na problemy systemowe.

## Odniesienia

- [gRPC website](https://www.cncf.io/projects/grpc/)
- [OpenTelemetry website](https://www.cncf.io/projects/opentelemetry/)
- [Grafana tools website](https://grafana.com/)
- [Protobuf website](https://protobuf.dev/)
