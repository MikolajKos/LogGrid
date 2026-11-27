# LogGrid: znane problemy

Stan na 10 listopada 2026, zaktualizowany 24 listopada 2026 po przejściu z wyszukiwania regexem na statystyki telemetryczne (gałąź `feat/telemetry-stats`). Uwagi z przeglądów PR #45 i z pracy nad testami mastera. Przy realizacji odznaczaj punkty albo przenoś je do zgłoszeń na GitHubie.

## Czas życia obiektów (krok 2 planu)

- [ ] **Destruktor `server_interface` woła `Stop()` za późno.** `~server_interface()` wykonuje się po zniszczeniu części `MasterServer`, a wątek asio wciąż działa i może wywołać `OnClientConnect` lub `OnClientDisconnect` na częściowo zniszczonym obiekcie. Obejście: wywoływać `Stop()` przed zniszczeniem mastera, tak jak w `TearDown` testów. Docelowo: `MasterServer` sam zatrzymuje serwer w swoim destruktorze albo właściciel zawsze woła `Stop()`.
- [x] Handlery w `net_connection.hpp` przechwytują surowe `this`. Naprawione w `7601add`: każdy handler trzyma `self = shared_from_this()`, a `client_interface` używa `shared_ptr`. ASan wykrywał ten błąd jako use-after-free w `~connection()`.
- [x] `client_interface::Disconnect()` woła `release()` zamiast `reset()`. Naprawione w `7601add`.
- [ ] **`WorkerClient` niszczy pulę wątków przed zatrzymaniem asio.** Pole `m_threadPool` jest niszczone w `~WorkerClient()`, a wątek asio zatrzymuje dopiero `~client_interface()`, który wykonuje się później. W tym oknie handler wiadomości może wrzucić zadanie do zniszczonej puli. Kierunek: destruktor `WorkerClient` najpierw woła `Disconnect()`.
- [ ] **`client_interface::Disconnect()` woła `m_context.stop()` zaraz po zleceniu zamknięcia gniazda.** Oczekujące handlery trzymają `shared_from_this()`, więc `reset()` nie niszczy połączenia, a zlecone `close` może się nie wykonać. Gniazdo zostaje otwarte do zniszczenia klienta i master nie widzi rozłączenia. Ponowne `Connect()` nie zadziała, bo kontekst nie jest restartowany.
- [ ] **`IsConnected()` czyta stan gniazda z innego wątku niż asio.** Pętla w `main` workera i `client_interface::Disconnect()` wołają `m_socket.is_open()`, a wątek asio może w tym czasie zamknąć gniazdo. To wyścig danych, którego TSan nie widzi, bo testy nie uruchamiają tej ścieżki. Kierunek: flaga `std::atomic<bool>` ustawiana w `HandleDisconnect`.

## Wykrywanie awarii i poprawność wyników

- [x] `OnClientDisconnect` nie jest wywoływany po crashu workera, więc zadania nie wracają do kolejki. Naprawione w `438f08c` i `7601add`: błąd odczytu lub zapisu trafia do kolejki jako zdarzenie rozłączenia, a martwe połączenie jest usuwane. Pokrywają to testy w `test_master_integration.cpp`, a `kill -9` workera na klastrze sprawdzono w 73 przebiegach (`bench/results/`).
- [ ] **Worker nie umie zgłosić nieudanego chunka.** Gdy nie otworzy pliku, `FileProcessor::ProcessChunk` zwraca pusty `ChunkResult` (`count = 0`, `errors = 0`), a master łączy go jak poprawny. Przy statystykach suma i `count` wychodzą za małe, a sesja kończy się stanem Done bez śladu błędu. Kierunek: licznik nieudanych chunków w `TelemetryStats`, np. `failed_chunks`, pokazywany w API. Do zrobienia przed pomiarami.
- [ ] Gdy łączenie wyniku się nie uda (`AggregateTaskResult` rzuca wyjątek przy nieznanym `search_id`), obsługa `TaskDone` i tak zwiększa `chunks_done`. Sesja może wtedy skończyć się stanem Done bez statystyk tego chunka.
- [x] Przy `count == 0` (pusta sesja albo same odrzucone linie) pola `min` i `max` w `TelemetryStats` mają wartości startowe `INT64_MAX` i `INT64_MIN`. JSON w `SearchController` nie może ich wtedy pokazywać jako prawdziwych wartości. Rozwiązane: `StatsToJson` wypisuje `min`, `max` i `mean` tylko przy `count > 0`.
- [ ] `m_workersFreeSlots` może przekręcić się poniżej zera przy równoległym przydzielaniu (`AssignIdleWorkers` czyta liczbę slotów pod lockiem, a wysyła po jego zwolnieniu).
- [ ] Komunikat `Reclaimed task` master wypisuje raz na workera i nie podaje, ile zadań wróciło do kolejki.

## Wydajność i pomiary

- [x] **`maxResults` jest limitem na chunk po stronie workera.** Każdy chunk może odesłać do `maxResults` pełnych linii, a master zapisuje tylko pierwsze `maxResults` w całej sesji i resztę odrzuca. Przy 400 chunkach to do 4 mln przesłanych linii. Nieaktualne po przejściu na statystyki: worker nie odsyła linii, a `maxResults` usunięto z API i protokołu.
- [ ] `FileProcessor` woła `tellg()` po każdej linii. Przy prostym przetwarzaniu linii to kilkukrotny narzut, więc pozycję lepiej liczyć z długości wczytanych danych.
- [x] `std::regex_search` po każdej linii przerabia około 64 MB/s na wątek, kilka razy wolniej niż zwykłe wyszukiwanie tekstu. Nieaktualne: w `dd00a00` regex zastąpiono parserem telemetrii. Pierwszy pomiar 24 listopada 2026 na laptopie: ok. 52 MB/s na CPU przy limicie 2 CPU (`PLAN_STYCZEN_2027.md`, tabela „Stan obecny”).
- [ ] **`bench/kill_worker.py` nie pasuje do nowego API.** Wysyła `keyword` i porównuje `totalMatches` z `grep -c`, a po przejściu na statystyki nie ma ani jednego, ani drugiego. Trzeba go przepisać na statystyki i program referencyjny Janka (osobny PR).
- [ ] **`bench/kill_worker.py` odczytuje stan klastra z tekstu logów mastera** (`"Hello message"`, `"Reclaimed task"`). Zmiana treści komunikatu, np. poprawka literówki „recived”, po cichu psuje skrypt: każdy przebieg z zabiciem staje się INVALID albo `wait_for_workers` kończy się limitem czasu. Kierunek: liczniki połączonych workerów, wykrytych awarii i odzyskanych zadań w `/api/status`, potrzebne i tak do metryk z kroku 4.
- [x] Worker liczy wątki z `hardware_concurrency()` − 1, które przy limicie `cpus` kontenera widzi wszystkie logiczne CPU maszyny. Na maszynie z 12 logicznymi CPU worker z limitem 2 CPU uruchamiał 11 wątków i zgłaszał 11 slotów, przez co pomiary skalowania były zniekształcone. Obejście w `ef94927`: zmienna `LOGGRID_WORKER_THREADS`, a `bench/kill_worker.py` ustawia ją zgodnie z `--worker-cpus`. Tryb automatyczny nadal nie widzi limitu.

## Protokół i bezpieczeństwo

- [ ] `ReadHeader` przyjmuje `header.size` z sieci bez limitu. Odczyt `msg >> batch` odrzuca za krótką wiadomość (wyjątek), ale za długą przyjmuje po cichu, bo czyta ostatnie `sizeof(ChunkResult)` bajtów.
- [ ] `ChunkResult` idzie przez sieć jako surowe bajty (`msg << batch`). `TelemetryStats` kończy się polem `bool`, więc wysyłane jest 7 bajtów wyrównania o nieokreślonej zawartości. Format zależy też od identycznego układu struktury po obu stronach, czyli od tego samego nagłówka i kompilatora. Na wynik to nie wpływa.
- [x] Długość linii w protokole wyników to `4` wpisane na sztywno po obu stronach. Nieaktualne: protokół nie przesyła już linii.
- [ ] `outputDir` sprawdzany jest leksykalnie bez końcowego separatora, więc ścieżka sąsiednia wychodzi poza `LOGGRID_OUTPUT_DIR`.
- [x] Słowo kluczowe obcinane po cichu do 63 znaków, a regex nie jest sprawdzany na masterze. Nieaktualne: `keyword` usunięto z API i protokołu.
- [ ] `filesystem_error` (np. pełny dysk wyjściowy) wraca do klienta jako 404 zamiast 500.

## Architektura: sprzężenie silnika z obliczeniem

- [ ] **Rodzaj obliczenia przenika przez wszystkie warstwy.** Przy przejściu z wyszukiwania regexem na statystyki telemetryczne zmieniają się `TaskPayload` (`keyword`, `max_results`), `ChunkResult`, sesja w `MasterServer` (`total_matches`, `line_count`, zapis linii do pliku w `WriteResults`), `ISearchService` (`StartSearch` z `keyword`, pola `SearchStatus`) i JSON w `SearchController`. Logika niezależna od obliczenia, czyli podział na chunki, przydział zadań, odzyskiwanie po awarii, odrzucanie spóźnionych i zduplikowanych `TaskDone` oraz kończenie sesji, jest przemieszana z kodem, który od obliczenia zależy. Każda zmiana obliczenia wymaga zmian w całym systemie.
  - Decyzja z 19 listopada 2026: granica na akumulatorze (wariant B). Wynik obliczenia to `TelemetryStats` z operacjami `Add` i `Merge`, czyli ten sam model co `AggregateFunction` we Flinku (`createAccumulator`, `add`, `merge`, `getResult`). Sesja w masterze przechowuje `TelemetryStats`, obsługa `TaskDone` po odrzuceniu duplikatów woła tylko `Merge`, a zamiana wyniku na JSON jest w jednej funkcji w `SearchController`. Kod przydziału zadań i odzyskiwania po awarii nie zna pól wyniku. Kolejna zmiana obliczenia dotyka parsera, typu statystyk, `FileProcessor` i funkcji JSON.
  - Nadal poza zakresem: kilka rodzajów obliczeń w jednym klastrze (wariant C), np. `MasterServer` jako szablon parametryzowany typem akumulatora albo rejestr typów zadań z identyfikatorem w `TaskPayload`. Wariant B jest do tego krokiem pośrednim. Temat na „dalsze prace”.

## Poza zakresem stycznia

- [ ] **Brak szyfrowania i uwierzytelniania.** Ruch TCP między masterem a workerami oraz API HTTP nie są szyfrowane. `OnClientConnect` przyjmuje każde połączenie, a API nie sprawdza, kto wysyła żądanie. Na czas pomiarów porty trzeba ograniczyć do sieci prywatnej, np. przez security groups na AWS. Kierunek docelowy: `asio::ssl` z OpenSSL i wzajemne uwierzytelnianie certyfikatami dla TCP, a dla HTTP TLS i token. Szyfrowanie zmienia narzut, więc porównanie z Flinkiem musi mieć tę samą konfigurację po obu stronach.
