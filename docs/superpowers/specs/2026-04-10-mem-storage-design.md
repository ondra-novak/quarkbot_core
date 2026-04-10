# Design: MemStorage — in-memory implementace IStorage

**Datum:** 2026-04-10  
**Účel:** Testovací in-memory implementace rozhraní `IStorage` / `IStorageTransaction` (viz `src/ifc/storage.hpp`).

---

## Motivace

Plánovaná produkční implementace bude nad LevelDB (WriteBatch model). Tato implementace slouží pro unit/integrační testy bez závislosti na disku.

---

## Datové struktury

Sekvenční a nesekvenční klíče mají **oddělené jmenné prostory** — klíč `"foo"` se `sequence=true` a `"foo"` se `sequence=false` jsou dva různé záznamy.

### Non-sequence namespace

```cpp
std::unordered_map<std::string, std::string> _plain;
```

- Smazání (`erase`) = odstranění záznamu z mapy.
- `get()` vrátí `Value{0, false, ""}` jak pro smazaný, tak pro neexistující klíč — z vnějšího pohledu je to totéž.
- Revize je vždy `0`.

### Sequence namespace

```cpp
struct SeqEntry {
    IStorage::Revision base_rev = 0;
    std::deque<std::pair<bool, std::string>> history;
    // history[i] odpovídá revizi base_rev + i + 1
};
std::unordered_map<std::string, SeqEntry> _seq;
```

- Aktuální revize = `base_rev + history.size()`.
- `put` přidá `{true, data}` na konec deque, vrátí novou revizi.
- `erase` přidá tombstone `{false, ""}` na konec deque, vrátí novou revizi.
- `prune_history(from, to)` odstraní záznamy z fronty deque pro revize `[from, min(to, last_rev-1)]` a posune `base_rev`. Poslední revize se **nikdy** nesmaže.

---

## Transakce: `MemStorageTransaction`

Write-batch model — transakce **nevidí vlastní zápisy**, pouze bufferuje operace. `commit()` je aplikuje atomicky.

```cpp
struct OpPut   { IStorage::Key key; std::string data; };
struct OpErase { IStorage::Key key; };
struct OpPrune { IStorage::Key key; IStorage::Revision from, to; };
using Op = std::variant<OpPut, OpErase, OpPrune>;

std::vector<Op> _ops;
MemStorage &_storage;
```

- `put(key, data)` → přidá `OpPut` do `_ops`, vrátí revizi vypočtenou z **aktuálního stavu storage v době volání** (ne po commitu): pro non-sequence vždy `0`; pro sequence `current_rev + 1` kde `current_rev = base_rev + history.size()`.
- `erase(key)` → přidá `OpErase`, vrátí revizi stejným způsobem.
- `prune_history(key, from, to)` → přidá `OpPrune`.
- `commit()` → iteruje `_ops` a aplikuje je přímo na `_storage`, pak vymaže `_ops`.

> **Souběžné zápisy (UB by design):** Pokud dvě transakce zapíšou do stejného sekvenčního klíče před tím, než jedna z nich commitne, obě obdrží stejné číslo revize (obě čtou stejný `current_rev`). Výsledek po commitu obou je undefined behavior — strategie musí zajistit vlastní synchronizaci zápisů. Toto **není chyba implementace**, je to vědomé rozhodnutí rozhraní (stejné chování bude mít i LevelDB implementace).

---

## `get_all_keys` — filtrování

```cpp
std::vector<std::string> get_all_keys(const Key &filter) const override;
```

- `filter.sequence` určuje, ze které mapy se vrátí klíče (`_plain` nebo `_seq`).
- `filter.name` je **prefix** — vrátí se jen klíče, jejichž název začíná tímto prefixem. Prázdný string = všechny klíče z dané mapy.

---

## Umístění souborů

```
src/impl/mem_storage.hpp   — hlavičkový soubor (celá implementace jako header-only)
```

Třída bude v namespace `quarkbot`, pattern `final` jako ostatní impl třídy.

---

## Co implementace záměrně neřeší

- Thread safety — není potřeba pro testy.
- Perzistence — data existují jen po dobu života objektu.
- Snapshot isolation mezi transakcemi — není potřeba (write-batch model).
- Řešení konfliktů při souběžných zápisech — UB by design, viz poznámka výše.
