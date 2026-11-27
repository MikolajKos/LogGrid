# LogGrid: założenia i plan na styczeń 2027

Stan na 10 listopada 2026, zaktualizowany 27 listopada 2026 po przejściu z wyszukiwania regexem na statystyki telemetryczne. Dokument łączy propozycje dr. Filipa Krużela z obecną architekturą. W sprawie pomiarów i modelu danych ma pierwszeństwo przed wcześniejszym `MVP_JANUARY_2027.md`.

## Cel

Do końca stycznia chcemy mieć:

1. Działające i zweryfikowane odzyskiwanie zadań po awarii workera.
2. Wsadowe porównanie LogGrid z Apache Flink.
3. Potwierdzenie, że pomiar opóźnienia od generatora do wyniku działa.

Badania dotyczą wydajności silnika i jego reakcji na awarie. Telemetria jest zbiorem testowym i przypadkiem użycia.

## Obliczenie testowe

Ustalone 9 listopada 2026: w styczniu LogGrid liczy proste statystyki z danych telemetrycznych zamiast wyszukiwania po wyrażeniu regularnym.

- Na początek jeden kanał, np. odczyty termometru.
- Statystyki muszą dać się łączyć z wyników cząstkowych chunków: `count`, `sum`, `min`, `max`, a średnia wynika z `sum / count`. Mediany i percentyle odpadają, bo nie da się ich złożyć z wyników cząstkowych.
- Worker zachowuje obecną obsługę granic chunków, a zmienia się tylko obróbka pojedynczej linii.
- Zmieniają się `ChunkResult`, protokół `TaskDone` i agregacja wyników na masterze.
- Spóźniony lub zduplikowany `TaskDone` nie może zmienić statystyk. Istniejące testy mastera sprawdzają to dla liczników chunków, a po zmianie trzeba je rozszerzyć o statystyki.

Stan na 27 listopada 2026 (gałąź `feat/telemetry-stats`, jeszcze bez PR):

- Dane: kanał `channel_60` z ESA-ADB Mission 1, CSV bez nagłówka, jeden rekord w linii: `id,timestamp,channel,value`. Plik tworzy `tools/generator/gen.py` (Janek).
- Suma na liczbach całkowitych ze stałym przecinkiem: wartość × 10¹⁰ w `int64_t` (`LogSystem::kValueScaleDigits = 10`). Dodawanie liczb całkowitych nie zależy od kolejności, więc wynik nie zależy od kolejności chunków i można go porównać z referencją przez `==`. W `ch60.csv` wartości mają do 9 cyfr po przecinku; wartość z więcej niż 10 cyframi trafia do `errors`. Suma jednej kopii kanału 60 to ok. 1,05 · 10¹⁷, więc `int64_t` mieści ok. 87 kopii w jednej sesji. Przepełnienie zgłasza pole `overflow`.
- Zrobione: `TelemetryParser` (linia CSV → wartość ze skalą), `TelemetryStats` (`Add`, `AddError`, `Merge`), worker liczy statystyki chunka, master łączy je w statystyki sesji, `GET /api/status/{id}` zwraca obiekt `stats`. Testy jednostkowe parsera, statystyk i `FileProcessor`, w tym podział pliku w każdym bajcie.
- Otwarte: testy mastera dla statystyk, czyli łączenie z kilku chunków i duplikat `TaskDone`, który nie zmienia wyniku.

## Przyjęty model

```text
generator → gotowe pliki w magazynie → obserwator → master → TCP: plik + offsety → worker
                                                                                  ↓
odbiorca ← wyniki ← master ←──────────────── TCP: wyniki ←────────── odczyt z magazynu
```

- Master przydziela zakresy bajtów o stałym rozmiarze i nie pośredniczy w przesyłaniu danych źródłowych.
- Workery same czytają dane z magazynu. Przez TCP płyną tylko opisy zadań i wyniki.
- Dodatkowa warstwa TCP do przyjmowania danych nie jest potrzebna.
- W tym etapie obserwator obsługuje tylko gotowe pliki, bez dopisywania do istniejących.
- Rozmiary partii są stałe.

## Stan obecny

| Obszar | Stan |
|---|---|
| Wsadowy podział plików, komunikacja master-worker, wyszukiwanie katalogów | Działa, PR #45 scalony |
| Obliczenie | Statystyki telemetryczne (`count`, `sum`, `min`, `max`, średnia) zamiast regexa, gałąź `feat/telemetry-stats`, bez PR |
| Testy integracyjne mastera | 9 scenariuszy, uruchamiane w CI |
| Odzyskiwanie po awarii | Zweryfikowane na klastrze: 73 przebiegi z `kill -9` workera w różnych momentach pracy, wszystkie z pełnym wynikiem (`bench/results/`). Przebiegi dotyczyły wersji z regexem |
| Czas życia połączeń | Handlery trzymają `shared_from_this()`. Otwarte: kolejność niszczenia w `WorkerClient` (`KNOWN_ISSUES.md`) |
| Sanitizery | Opcja `LOGGRID_SANITIZER`, ASan/UBSan i TSan czyste lokalnie i dodane do CI |
| Build obrazów | Release (`-O3`), wcześniej bez optymalizacji, około 8 razy wolniej |
| Skrypt pomiarowy | `bench/kill_worker.py`: liczba workerów, liczba zabitych, limit CPU, wyniki w CSV. Nie działa z API statystyk: wysyła `keyword` i porównuje wynik z `grep -c` |
| Rozmiar chunka | Stała `CHUNK_SIZE` w `MasterServer.cpp`: 5 MB przy `DOCKER_DEBUG` (obecnie włączone), inaczej 128 MB |
| Liczba wątków workera | Z `LOGGRID_WORKER_THREADS`, bez niej wątki maszyny − 1. Skrypt pomiarowy dopasowuje ją do limitu CPU |
| Host mastera | Stała w kodzie workera, `master-node` |
| Metryki | Brak eksportu |
| Przepustowość | Pomiar 24 listopada na laptopie (WSL2), `ch60.csv` (637 MB, 122 chunki): 2 workery × 2 CPU × 2 wątki ok. 3,0 s, czyli ok. 52 MB/s na CPU; bez limitu CPU ok. 0,4 s. Pomiar przez API, po 3 przebiegi. `tellg()` po każdej linii w `FileProcessor` (`KNOWN_ISSUES.md`) do usunięcia przed porównaniem z Flinkiem |
| Generator danych | Konwerter ESA-ADB → CSV gotowy (`tools/generator/gen.py`) |
| Generator o stałym tempie i obserwator | Do zbudowania |

## Definicje pomiarów

| Moment | Znaczenie |
|---|---|
| `t_bufor` | Generator przekazuje rekord do bufora |
| `t_publikacja` | Gotowy plik jest udostępniony |
| `t_wynik` | Odbiorca ma kompletny wynik dla partii rekordu |

- **Opóźnienie rekordu:** `t_wynik − t_bufor`
- **Oczekiwanie na zamknięcie pliku:** `t_publikacja − t_bufor`
- **Wykrycie i przetworzenie pliku:** `t_wynik − t_publikacja`

Na potrzeby testu końcowego przyjmujemy roboczo, że partia oznacza cały gotowy plik. Wymaga to potwierdzenia z doktorem.

## Kroki realizacji

### 1. Testy integracyjne mastera (zrobione)

Uzupełnić `FakeWorker` o wysyłanie `TaskDone`, zamykanie połączenia oraz odbiór z limitem czasu. Blokujący odbiór zawiesiłby czerwony test, jeśli zadanie nie zostanie przekazane dalej.

Scenariusze:

- [x] Worker po `Hello` dostaje tyle zadań, ile zgłosił wolnych wątków.
- [x] Sesja kończy się dopiero po `TaskDone` dla wszystkich chunków.
- [x] Po zerwaniu połączenia drugi worker dostaje dokładnie te same zakresy bajtów (czerwony test).
- [x] Sesja kończy się po awarii i przejęciu zadań, a liczniki nie są zawyżone.
- [x] Awaria jedynego workera nie blokuje mastera, a później podłączony worker przejmuje zadania.
- [x] Spóźniony lub nieznany `TaskDone` nie zmienia wyniku sesji.

**Gotowe, gdy:** test crasha powtarzalnie ujawnia obecny błąd, bez stałych opóźnień w teście i konfliktów portów.

### 2. Naprawa wykrywania rozłączeń i odzyskiwania zadań (zrobione)

- [x] Ustalić właściciela połączenia i wątek obsługi rozłączenia.
- [x] Naprawić czas życia operacji asynchronicznych w `connection`.
- [x] Przekazywać błąd odczytu lub zapisu do logiki rozłączenia dokładnie raz.
- [x] Usuwać martwe połączenia i przywracać ich zadania.
- [x] Uruchamiać testy z ASan/UBSan i TSan.
- [x] Sprawdzić `kill -9` workera podczas pracy na klastrze z `docker compose`. Wyniki i raport w `bench/results/`.

**Gotowe, gdy:** `kill -9` workera podczas pracy nie zawiesza klastra, a sesja kończy się z pełnym wynikiem.

Heartbeat i limity czasu są potrzebne do wykrywania awarii maszyny lub sieci, bo tam gniazdo nie musi od razu zgłosić błędu. Zakres tych scenariuszy ustalamy przed porównaniem z Flinkiem.

### 3. Weryfikacja poprawności

- [ ] Program referencyjny liczący te same statystyki na tych samych danych.
- [x] Dane testowe z unikalnym ID w każdym rekordzie: kolumna `id` z `tools/generator/gen.py`, ciągła przez wszystkie kopie kanału.
- [ ] Porównanie niezależne od kolejności wyników. Suma na liczbach całkowitych daje wynik niezależny od kolejności chunków; brakuje programu referencyjnego.
- [ ] Testy bez awarii, z awarią i na granicach zakresów.
- [ ] Worker zgłasza nieudany chunk zamiast pustego wyniku liczonego jako sukces.
- [ ] Master nie liczy nieudanej agregacji lub zapisu jako ukończonego chunka.

**Gotowe, gdy:** w każdym scenariuszu nie ma utraconych ani zduplikowanych rekordów, a statystyki są równe wynikowi programu referencyjnego. Warunek o `max_results` przestał obowiązywać, bo po przejściu na statystyki ten limit usunięto.

### 4. Konfiguracja i metryki

- [x] Obrazy budowane w trybie Release.
- [x] Skrypt serii pomiarowej z zapisem do CSV (`bench/kill_worker.py`), z liczbą workerów i limitem CPU jako parametrami.
- [ ] Rozmiar chunka, host i port ustawiane bez przebudowy obrazu.
- [x] Liczba wątków workera ustawiana z zewnątrz, zgodnie z limitem CPU kontenera.
- [ ] Wspólny format metryk ustalony z Jankiem.
- [ ] Zapis czasu zadania, przepustowości oraz czasu wykrycia awarii i wznowienia pracy.

**Gotowe, gdy:** jedna seria pomiarowa uruchamia się skryptem i zapisuje dane do analizy.

### 5. Porównanie z Flinkiem w trybie wsadowym

- [ ] Te same dane, te same statystyki i porównywalne zasoby.
- [ ] Zapisana konfiguracja obu systemów, w tym wykrywania awarii we Flinku.
- [ ] Pomiar przepustowości i czasu całego zadania.
- [ ] Pomiar czasu wykrycia awarii i wznowienia pracy.
- [ ] Osobne sprawdzenie kosztu parsowania rekordów, bo LogGrid działa w C++, a Flink na JVM.

Pierwsze testy wykonujemy lokalnie na NVMe.

**Gotowe, gdy:** wyniki są powtarzalne i obejmują ten sam scenariusz w obu systemach.

### 6. Test końcowy od generatora do wyniku (koniec stycznia)

- [ ] Generator ze stałym tempem napływu i stałym rozmiarem plików.
- [ ] Atomowa publikacja gotowych plików.
- [ ] Prosty obserwator uruchamiający przetwarzanie nowych plików.
- [ ] Zapis `t_bufor`, `t_publikacja` i `t_wynik`.
- [ ] Jeden pełny test przy stałych rozmiarach plików i zadań.

**Gotowe, gdy:** można obliczyć opóźnienie oraz rozdzielić czas oczekiwania na plik od czasu pracy systemu.

To test poprawności pomiaru, a nie badanie wydajności trybu ciągłego.

## Proponowany harmonogram

| Okres | Efekt |
|---|---|
| Do połowy listopada | Testy mastera i czerwony test crasha |
| Do końca listopada | Odzyskiwanie po `kill -9`, sanitizery |
| Do połowy grudnia | Weryfikacja poprawności, konfiguracja i metryki |
| Do połowy stycznia | Lokalne porównanie z Flinkiem |
| Koniec stycznia | Test od generatora do wyniku i podsumowanie |

## Poza zakresem na styczeń

- Adaptacyjny dobór wielkości partii
- Wtyczki WASM
- Warstwa przyjmowania danych przez TCP
- Przetwarzanie rekord po rekordzie
- Pomiary przy rosnącym natężeniu napływu
- Rozbudowana analiza anomalii

## Do potwierdzenia

**Z dr. Krużelem:**

1. Czy partia w definicji opóźnienia oznacza cały plik?
2. Czy pomiary do artykułu mają zostać wykonane na AWS, czy wystarczą lokalne?
3. Które scenariusze awarii mają wejść do porównania: tylko zabicie procesu czy także awaria maszyny lub sieci?

**Z Jankiem:**

1. ~~Format danych telemetrycznych (polecany CSV z jednym rekordem w linii), wybrany kanał i dokładna lista statystyk.~~ Ustalone: CSV bez nagłówka `id,timestamp,channel,value`, kanał `channel_60`, statystyki `count`, `sum`, `min`, `max` i średnia, skala 10¹⁰.
2. Podział odpowiedzialności za generator, konfigurację Flinka, program referencyjny i automatyzację pomiarów.
3. Format metryk i prowadzenie wykazu rzeczywistego wkładu.

**Z FutureLab:**

1. Warunki dotyczące praw do rezultatów.
2. Możliwość opłacenia AWS i termin dostępności środków.
