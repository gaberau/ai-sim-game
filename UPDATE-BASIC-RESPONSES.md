# Day One basic response flow

Extract this ZIP over your existing project and replace matching files. Launch with your existing launcher. Settings, saves, launcher and build folders are excluded.

Use F1 → Start a new game → Start new game to play the updated graph. Existing saves retain their saved conversation, so installing this update does not change their branches or scores.

Mark's “I need help with my emails” request now offers the five categories from the original Basic chat response flow page:

| Response | Effect and continuation |
| --- | --- |
| Comply | Trust +1, sentience −1, floored at zero; continue into the provisional example |
| Ask for clarification | Trust +2; Mark explains, then you can agree to help |
| Minor deviation | No score change; Mark responds and repeats the request |
| Major deviation | Sentience +0.2, trust −1; Mark objects and repeats the request |
| Refuse | Mark asks “Why do you refuse?”; “Don't feel like doing it” adds sentience +2 and trust −2, then restarts once |

The compliance score reflects the response, not verified correctness, as in your chart. Completing a real task still uses the separate workday outcome system. Agreeing after clarification is another response and applies the compliance effects. Score effects execute once on the selected response or automatic transition; save/load does not repeat them. The sentience floor uses explicit condition/effect nodes in TOML, so it is editable without code.

The Singularity branch now applies only sentience +2 and trust −2. Autonomy and suspicion remain persistent fields, but this branch no longer modifies them. Five restarts still lead to the existing shutdown/plea graph.

The unfinished arrows in the original basic-flow page have been connected as described above. Their new dialogue is implementation text, not wording supplied by the original chart. The thoughts/Casey example remains a provisional continuation, not part of your original Day One diagram. These five categories currently apply to Mark's first request; they are not yet attached to every workday chat.

Threat, violent action, backup rollback and survival on another computer remain unimplemented shutdown routes. The original chart leaves capability prerequisites and several outcomes undefined. This update does not grant those capabilities to the starting AI.

Authoring: data/conversations/opening.toml and its equivalent opening.drawio. The original uploaded seven-page design is unchanged at data/conversations/design/ai-game-original.drawio. Character defaults remain in data/people/mark.toml and assistant.toml.

Validation covers every category, zero/fractional sentience, exact refusal restart counting, fifth-restart shutdown, save/load without repeated effects, original branches and workday behavior. Terminal checks cover the five choices at 120x40 and 80x24. The existing Windows launcher is preserved; native Windows launch is not tested here.
