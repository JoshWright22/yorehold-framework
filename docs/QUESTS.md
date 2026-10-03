# Writing a chapter's quest journal

Edit `assets/quests/keep.json` and open **F10 Quests** in the framework browser. The scene demonstrates discovery, partial objective progress, completion, failure and resetting chapter state. Quests use the same story flag names as `assets/dialogue/gatekeeper.json`, so dialogue can reveal or complete a journal entry without another scripting system.

A document has a `quests` array. Each quest has a unique `id`, `title`, optional `description` and one or more `objectives`. Quest and objective ids stay stable when writers edit their wording.

| Field | Meaning |
|---|---|
| Quest `require` | All listed flags must be set before the quest appears; absent or empty makes it visible immediately |
| Quest `fail` | Any listed flag marks a visible quest failed |
| Objective `require` | All listed flags must be set before the objective is complete |

An objective has its own `id` and visible `text`; it needs at least one completion flag. Objectives may be completed in any order. A quest is completed once every objective is complete. Failure takes priority over completion for a discovered quest. An undiscovered quest stays hidden even if its failure or completion flags have already been set.

```cpp
auto text = files.readText("quests/keep.json");
if (!text) return;
auto journal = yh::QuestJournal::fromJson(*text, &error);
if (!journal) return; // show the author the validation message
for (const auto& entry : journal->entries(chapterFlags))
{
    // entry.quest holds the title, description and authored objectives.
    // entry.progress holds the status and completion bit for each objective.
    // entry.progress.fraction() is useful for a progress bar.
}
```

The chapter owns the flags. Dialogue can change them, a world interaction can set `prisoners_safe`, and combat can set `chief_defeated`. The journal derives its view from the current flags each time; it does not keep a separate mutable copy of quest progress. Saving and restoring those flags therefore saves and restores the journal too. Unit tests cover this using a real dialogue checkpoint.

Entries borrow pointers from the authored journal. Keep the journal alive and its quest definitions unchanged while using a returned view. `entries(flags, true)` also includes hidden quests for authoring tools. `quest(id)` looks up a definition, and `Quest::progress(flags)` evaluates a single quest. Unknown quest ids return null.

Loading validates unique quest/objective ids, non-empty titles and objective text, completion requirements, JSON types and distinct non-empty flags. Empty journals are allowed. The parser limits a document to 4 MB, 512 quests and 128 objectives per quest.

The journal runtime and its test scene are ready for the client to attach to its chapter state. Rewards, inventory changes and notifications remain client decisions; a journal view never grants rewards while it is being drawn.

```powershell
./yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --scene 'F10 Quests' --hidden --no-vsync --frames 50 --fixed-dt 0.0166666667 --input yorehold-framework/tests/visual/scripts/quests.txt
```

Verified on Windows/MSVC: the framework and browser build without warnings; all 2,350 regression checks pass across 22 groups. The 50-frame script captures hidden, active, partial, completed, failed and reset journals without GPU validation errors.
