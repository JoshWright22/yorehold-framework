# Writing chapter dialogue

`assets/dialogue/gatekeeper.json` is a complete conversation you can edit. Open **F9 Dialogue** in the framework browser to play it. Replies branch to node ids; story flags hide or reveal replies, and checks choose a success or failure destination.

Each document has an `id`, a `start` node id, and a `nodes` array. Each node has an `id`, optional `speaker`, `text`, and optional `choices`. A node without choices is the final line. A choice has its own `id`, visible `text`, and `next` node id. An empty `next` ends the conversation immediately. Loops are allowed: a guard can ask you to come back, or a player can ask another question.

Optional fields on a choice:

| Field | Meaning |
|---|---|
| `require` | All listed story flags must be set for this reply to appear |
| `forbid` | The reply is hidden if any listed flag is set |
| `set`, `clear` | Flags changed after the player chooses this reply |
| `check` | An object with `skill`, integer `difficulty`, `success` and `failure` node ids |

Nodes can also have `set` and `clear`, applied when the player enters that node. This lets the success and failure lines have different consequences. Flag ids can be anything meaningful to the chapter, such as `keep_gate_open` or `rescued_miller`.

```cpp
auto text = files.readText("dialogue/gatekeeper.json");
if (!text) return; // show the missing-file message
auto authored = yh::Dialogue::fromJson(*text, &error);
if (!authored) return; // show error to the chapter author
yh::DialogueSession conversation(std::move(*authored), existingStoryFlags);
auto result = conversation.choose("persuade", [&](std::string_view skill) {
    return hero.rollCheck(rules, skill, yh::Advantage::None, random);
});
// Render conversation.current() and conversation.choices().
// Copy conversation.flags() back to the chapter when the conversation ends.
```

The session owns its authored data. Pointers returned by `current()` and `choices()` remain valid until that session is destroyed. A check uses the client-supplied roller, so the same seeded `yh::Random` can serve combat and dialogue. A skill check succeeds when its total meets the difficulty; a natural 1 or 20 does not override that comparison.

Unknown or currently hidden choices return no result and change nothing. A checked choice without a roller also changes nothing. If a roller throws, the conversation stays on the same node with the same flags and history. Accepted choices append a `DialogueResult` containing the old/new node ids, choice id, and any roll, so the client can show a transcript or send a command to other players.

Loading validates unique ids, all destinations, flag contradictions, check difficulties and JSON types. Node ids should survive text edits so saved progress can continue to refer to the same authored line. The runtime currently lives in the framework and its browser; the client can attach it to an NPC interaction.

## Conversation checkpoints

`conversation.snapshot()` returns versioned JSON with the dialogue id, current node id and all story flags. Store it with the rest of the chapter save. `conversation.restore(saved, &error)` validates the entire checkpoint before changing the session. A wrong dialogue, removed node, unsupported version or malformed flags fails without changing the conversation. Restoring a terminal line and a closed conversation both work.

Restoring uses the saved flags directly; it does not repeat node-entry effects. The local choice history is cleared on a successful restore. Save your transcript and the client's random generator separately if you need to preserve them. F9 demonstrates this by keeping a copy of its seeded generator beside the checkpoint, so replaying a check after a restore gives the same roll.

Press **S** to save a checkpoint and **L** to restore it in F9. The buttons do the same thing. A replay restarts the conversation; it keeps the saved checkpoint available until you replace it.

Run the focused regressions with the normal framework CTest target. The visual script walks the rumor branch, opens the gate, then replays and makes a character check:

```powershell
./yorehold/out/bin/RelWithDebInfo/yorehold-framework-tests.exe --scene 'F9 Dialogue' --hidden --no-vsync --frames 45 --fixed-dt 0.0166666667 --input yorehold-framework/tests/visual/scripts/dialogue.txt
```

Verified on Windows/MSVC: the framework and visual browser build without warnings; all 2,320 checks pass across 21 groups and CTest passes. The dialogue script saves greeting, rumor, gate-open, restored and check-result screenshots without GPU validation errors.
