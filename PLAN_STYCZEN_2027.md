# LogGrid: założenia i plan na styczeń 2027

Stan na 6 listopada 2026. Dokument łączy propozycje dr. Filipa Krużela z obecną architekturą. W sprawie pomiarów i modelu danych ma pierwszeństwo przed wcześniejszym `MVP_JANUARY_2027.md`.

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
| Testy integracyjne mastera | W trakcie, gałąź `test/master-integration` |
| Odzyskiwanie po awarii | Logika istnieje, ale błąd odczytu tylko zamyka gniazdo i nie uruchamia `OnClientDisconnect` |
| Czas życia połączeń | Handlery w `net_connection.hpp` przechwytują surowe `this` |
| Rozmiar chunka | Stała w kodzie, obecnie 5 MB |
| Host mastera | Stała w kodzie workera, `master-node` |
| Metryki | Brak eksportu |
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

### 1. Testy integracyjne mastera (w trakcie)

Uzupełnić `FakeWorker` o wysyłanie `TaskDone`, zamykanie połączenia oraz odbiór z limitem czasu. Blokujący odbiór zawiesiłby czerwony test, jeśli zadanie nie zostanie przekazane dalej.

Scenariusze:

- [ ] Worker po `Hello` dostaje tyle zadań, ile zgłosił wolnych wątków.
- [ ] Sesja kończy się dopiero po `TaskDone` dla wszystkich chunków.
- [ ] Po zerwaniu połączenia drugi worker dostaje dokładnie te same zakresy bajtów (czerwony test).
- [ ] Sesja kończy się po awarii i przejęciu zadań, a liczniki nie są zawyżone.
- [ ] Awaria jedynego workera nie blokuje mastera, a później podłączony worker przejmuje zadania.
- [ ] Spóźniony lub nieznany `TaskDone` nie zmienia wyniku sesji.

**Gotowe, gdy:** test crasha powtarzalnie ujawnia obecny błąd, bez stałych opóźnień w teście i konfliktów portów.

### 2. Naprawa wykrywania rozłączeń i odzyskiwania zadań

- [ ] Ustalić właściciela połączenia i wątek obsługi rozłączenia.
- [ ] Naprawić czas życia operacji asynchronicznych w `connection`.
- [ ] Przekazywać błąd odczytu lub zapisu do logiki rozłączenia dokładnie raz.
- [ ] Usuwać martwe połączenia i przywracać ich zadania.
- [ ] Uruchamiać testy z ASan/UBSan i TSan.

**Gotowe, gdy:** `kill -9` workera podczas pracy nie zawiesza klastra, a sesja kończy się z pełnym wynikiem.

Heartbeat i limity czasu są potrzebne do wykrywania awarii maszyny lub sieci, bo tam gniazdo nie musi od razu zgłosić błędu. Zakres tych scenariuszy ustalamy przed porównaniem z Flinkiem.

### 3. Weryfikacja poprawności

- [ ] Program referencyjny liczący te same statystyki na tych samych danych.
- [ ] Dane testowe z unikalnym ID w każdym rekordzie.
- [ ] Porównanie niezależne od kolejności wyników.
- [ ] Testy bez awarii, z awarią i na granicach zakresów.
- [ ] Worker zgłasza nieudany chunk zamiast pustego wyniku liczonego jako sukces.
- [ ] Master nie liczy nieudanej agregacji lub zapisu jako ukończonego chunka.

**Gotowe, gdy:** w każdym scenariuszu nie ma utraconych ani zduplikowanych rekordów, a `max_results` nie obcina wyników.

### 4. Konfiguracja i metryki

- [ ] Rozmiar chunka, host i port ustawiane bez przebudowy obrazu.
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

1. Format danych telemetrycznych (polecany CSV z jednym rekordem w linii), wybrany kanał i dokładna lista statystyk.
2. Podział odpowiedzialności za generator, konfigurację Flinka, program referencyjny i automatyzację pomiarów.
3. Format metryk i prowadzenie wykazu rzeczywistego wkładu.

**Z FutureLab:**

1. Warunki dotyczące praw do rezultatów.
2. Możliwość opłacenia AWS i termin dostępności środków.
