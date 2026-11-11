# LogGrid: znane problemy

Stan na 10 listopada 2026. Uwagi z przeglądów PR #45 i z pracy nad testami mastera. Przy realizacji odznaczaj punkty albo przenoś je do zgłoszeń na GitHubie.

## Czas życia obiektów (krok 2 planu)

- [ ] **Destruktor `server_interface` woła `Stop()` za późno.** `~server_interface()` wykonuje się po zniszczeniu części `MasterServer`, a wątek asio wciąż działa i może wywołać `OnClientConnect` lub `OnClientDisconnect` na częściowo zniszczonym obiekcie. Obejście: wywoływać `Stop()` przed zniszczeniem mastera, tak jak w `TearDown` testów. Docelowo: `MasterServer` sam zatrzymuje serwer w swoim destruktorze albo właściciel zawsze woła `Stop()`.
- [x] Handlery w `net_connection.hpp` przechwytują surowe `this`. Naprawione w `7601add`: każdy handler trzyma `self = shared_from_this()`, a `client_interface` używa `shared_ptr`. ASan wykrywał ten błąd jako use-after-free w `~connection()`.
- [x] `client_interface::Disconnect()` woła `release()` zamiast `reset()`. Naprawione w `7601add`.
- [ ] **`WorkerClient` niszczy pulę wątków przed zatrzymaniem asio.** Pole `m_threadPool` jest niszczone w `~WorkerClient()`, a wątek asio zatrzymuje dopiero `~client_interface()`, który wykonuje się później. W tym oknie handler wiadomości może wrzucić zadanie do zniszczonej puli. Kierunek: destruktor `WorkerClient` najpierw woła `Disconnect()`.
- [ ] **`client_interface::Disconnect()` woła `m_context.stop()` zaraz po zleceniu zamknięcia gniazda.** Oczekujące handlery trzymają `shared_from_this()`, więc `reset()` nie niszczy połączenia, a zlecone `close` może się nie wykonać. Gniazdo zostaje otwarte do zniszczenia klienta i master nie widzi rozłączenia. Ponowne `Connect()` nie zadziała, bo kontekst nie jest restartowany.
- [ ] **`IsConnected()` czyta stan gniazda z innego wątku niż asio.** Pętla w `main` workera i `client_interface::Disconnect()` wołają `m_socket.is_open()`, a wątek asio może w tym czasie zamknąć gniazdo. To wyścig danych, którego TSan nie widzi, bo testy nie uruchamiają tej ścieżki. Kierunek: flaga `std::atomic<bool>` ustawiana w `HandleDisconnect`.

## Wykrywanie awarii i poprawność wyników

- [x] `OnClientDisconnect` nie jest wywoływany po crashu workera, więc zadania nie wracają do kolejki. Naprawione w `438f08c` i `7601add`: błąd odczytu lub zapisu trafia do kolejki jako zdarzenie rozłączenia, a martwe połączenie jest usuwane. Pokrywają to testy w `test_master_integration.cpp`, a `kill -9` workera na klastrze sprawdzono w 73 przebiegach (`bench/results/`).
- [ ] Worker nie umie zgłosić nieudanego chunka (brak pliku, błędny regex) i zwraca pusty wynik liczony jako sukces.
- [ ] Nieudana agregacja lub zapis wyników i tak zwiększa `chunks_done`; `linesCount` może pokazać linie, których nie zapisano.
- [ ] `m_workersFreeSlots` może przekręcić się poniżej zera przy równoległym przydzielaniu (`AssignIdleWorkers` czyta liczbę slotów pod lockiem, a wysyła po jego zwolnieniu).
- [ ] Komunikat `Reclaimed task` master wypisuje raz na workera i nie podaje, ile zadań wróciło do kolejki.

## Wydajność i pomiary

- [ ] **`maxResults` jest limitem na chunk po stronie workera.** Każdy chunk może odesłać do `maxResults` pełnych linii, a master zapisuje tylko pierwsze `maxResults` w całej sesji i resztę odrzuca. Przy 400 chunkach to do 4 mln przesłanych linii. Hipoteza, niezmierzona: sprawdzić czas sesji z `maxResults: 0`.
- [ ] `FileProcessor` woła `tellg()` po każdej linii. Przy prostym przetwarzaniu linii to kilkukrotny narzut, więc pozycję lepiej liczyć z długości wczytanych danych.
- [ ] `std::regex_search` po każdej linii przerabia około 64 MB/s na wątek, kilka razy wolniej niż zwykłe wyszukiwanie tekstu. Zniknie przy przejściu na statystyki telemetryczne.
- [ ] **`bench/kill_worker.py` odczytuje stan klastra z tekstu logów mastera** (`"Hello message"`, `"Reclaimed task"`). Zmiana treści komunikatu, np. poprawka literówki „recived”, po cichu psuje skrypt: każdy przebieg z zabiciem staje się INVALID albo `wait_for_workers` kończy się limitem czasu. Kierunek: liczniki połączonych workerów, wykrytych awarii i odzyskanych zadań w `/api/status`, potrzebne i tak do metryk z kroku 4.
- [ ] Worker liczy wątki z `hardware_concurrency()`, które przy limicie `cpus` kontenera widzi wszystkie rdzenie maszyny. Worker z limitem 2 CPU uruchamia 11 wątków i zgłasza 11 slotów, przez co pomiary skalowania są zniekształcone.

## Protokół i bezpieczeństwo

- [ ] `ReadHeader` przyjmuje `header.size` z sieci bez limitu, a `DeserializeBatch` nie sprawdza granic bufora.
- [ ] Długość linii w protokole wyników to `4` wpisane na sztywno po obu stronach; warto zdefiniować wspólny typ w `LogSearchCommon.hpp`.
- [ ] `outputDir` sprawdzany jest leksykalnie bez końcowego separatora, więc ścieżka sąsiednia wychodzi poza `LOGGRID_OUTPUT_DIR`.
- [ ] Słowo kluczowe obcinane po cichu do 63 znaków, a regex nie jest sprawdzany na masterze.
- [ ] `filesystem_error` (np. pełny dysk wyjściowy) wraca do klienta jako 404 zamiast 500.

## Poza zakresem stycznia

- [ ] **Brak szyfrowania i uwierzytelniania.** Ruch TCP między masterem a workerami oraz API HTTP nie są szyfrowane. `OnClientConnect` przyjmuje każde połączenie, a API nie sprawdza, kto wysyła żądanie. Na czas pomiarów porty trzeba ograniczyć do sieci prywatnej, np. przez security groups na AWS. Kierunek docelowy: `asio::ssl` z OpenSSL i wzajemne uwierzytelnianie certyfikatami dla TCP, a dla HTTP TLS i token. Szyfrowanie zmienia narzut, więc porównanie z Flinkiem musi mieć tę samą konfigurację po obu stronach.
