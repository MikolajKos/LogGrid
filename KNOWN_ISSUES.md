# LogGrid: znane problemy

Stan na 8 listopada 2026. Uwagi z przeglądów PR #45 i z pracy nad testami mastera. Przy realizacji odznaczaj punkty albo przenoś je do zgłoszeń na GitHubie.

## Czas życia obiektów (krok 2 planu)

- [ ] **Destruktor `server_interface` woła `Stop()` za późno.** `~server_interface()` wykonuje się po zniszczeniu części `MasterServer`, a wątek asio wciąż działa i może wywołać `OnClientConnect` lub `OnClientDisconnect` na częściowo zniszczonym obiekcie. Obejście: wywoływać `Stop()` przed zniszczeniem mastera, tak jak w `TearDown` testów. Docelowo: `MasterServer` sam zatrzymuje serwer w swoim destruktorze albo właściciel zawsze woła `Stop()`.
- [ ] Handlery w `net_connection.hpp` przechwytują surowe `this`. Przed usuwaniem martwych połączeń trzeba przejść na `shared_from_this()`, a `client_interface` na `shared_ptr` zamiast `unique_ptr`.
- [ ] `client_interface::Disconnect()` woła `release()` zamiast `reset()`, a zamknięcie gniazda przez `post` może się nie wykonać przed `stop()`.

## Wykrywanie awarii i poprawność wyników

- [ ] `OnClientDisconnect` nie jest wywoływany po crashu workera, więc zadania nie wracają do kolejki.
- [ ] Worker nie umie zgłosić nieudanego chunka (brak pliku, błędny regex) i zwraca pusty wynik liczony jako sukces.
- [ ] Nieudana agregacja lub zapis wyników i tak zwiększa `chunks_done`; `linesCount` może pokazać linie, których nie zapisano.
- [ ] `m_workersFreeSlots` może przekręcić się poniżej zera przy równoległym przydzielaniu (`AssignIdleWorkers` czyta liczbę slotów pod lockiem, a wysyła po jego zwolnieniu).

## Protokół i bezpieczeństwo

- [ ] `ReadHeader` przyjmuje `header.size` z sieci bez limitu, a `DeserializeBatch` nie sprawdza granic bufora.
- [ ] Długość linii w protokole wyników to `4` wpisane na sztywno po obu stronach; warto zdefiniować wspólny typ w `LogSearchCommon.hpp`.
- [ ] `outputDir` sprawdzany jest leksykalnie bez końcowego separatora, więc ścieżka sąsiednia wychodzi poza `LOGGRID_OUTPUT_DIR`.
- [ ] Słowo kluczowe obcinane po cichu do 63 znaków, a regex nie jest sprawdzany na masterze.
- [ ] `filesystem_error` (np. pełny dysk wyjściowy) wraca do klienta jako 404 zamiast 500.
