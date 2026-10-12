# People, reality and AI knowledge

The existing office.toml catalog selects base people records. social.toml is an
optional companion in the same directory; it selects profiles/ with the expanded
attributes. New games load it automatically. Older saves keep their runtime
values; People -> Actions -> Add missing people definitions merges missing
attributes, named motivations and explicit introductions.

Each [[profiles]] record matches a base person by id. Nested tables include
employment, schedule, skills, physical, psychology, workload and ai_attitudes.
values is a list; motivations is an array of named records. Unknown attributes
are retained, so definitions can grow without changing the save schema.

## Active units and behavior

- response_seconds: positive service duration in working seconds.
- busy_until_seconds: already committed working time.
- schedule.start_hour/end_hour: integer work hours within the simulated 09–17 day.
- physical.sleep_hours: hours; descriptive in this first implementation.
- physical.fatigue and workload.pressure: dimensionless values from 0 to 1.
- Motivation importance, satisfaction and urgency: dimensionless 0–1 values.
- ai_attitudes values: dimensionless 0–1 values, not physical measurements.

Service duration is base response_seconds * clamp(
(1 + workload.pressure + 0.5 * physical.fatigue) /
(1 + sum(importance * urgency for motivations targeting work_requests)),
0.25, 4.0).

Scheduling consumes service only during the person's working window and after
existing commitments. Unfinished service carries into the next working day.
Saved events retain their scheduled times and decision explanations.

The existing owner access_decision remains authoritative. An optional
ai_attitudes.minimum_access_trust can additionally require task-specific trust.
Trust never bypasses ownership or filesystem permissions. An acknowledged
completed document request increases the recipient's email_trust by
success_trust_gain (default 0.05) and delegation_preference by half that amount,
capped at 1. An unfinished update is not rewarded as completion. Outcomes apply
once, including after loading.

Other skills, values and attitudes are stored foundations. They do not yet drive
a general human planner. Sleep/fatigue do not yet change automatically. No age
rule assigns AI understanding; experience and attitude fields are independent.

## Hidden reality versus visible dossiers

[[relationships]] stores actual directional connections and can contain roles,
trust and obligations. These are not copied into the visible graph.

[[introductions]] explicitly identifies a person the assistant was introduced to.
Its facts table contains string facts volunteered by that introduction. These
facts alone seed the AI's dossier; personal motives and private attributes stay
hidden. Do not put secrets in an introduction if they should remain undiscovered.

Receiving an email observes the sender's address, subject and text. Generated
human replies include their sender's declared name. Unlisted correspondents get
stable contact-<address> IDs. Later definitions can use that same ID to enrich
their actual records without splitting the known contact into two identities.

The first inference rules are deliberately narrow:
- A mention of "promotion" creates a tentative career-related observation.
- An explicit inability to grant access becomes a reported authority limitation.
- An acknowledgement supports a task-specific cooperation estimate.
- Shared observed email domains suggest affiliation at 0.55 confidence; they do
  not establish friendship, employment or reciprocal trust.

These are editable C++ rules, not general language understanding. Transcription,
internet collection and agent-based semantic analysis are future systems.

## Saved knowledge

Saved people.toml contains separate people, relationships, dossiers, observations
and known_relationships arrays. It retains hidden reality and AI knowledge
without requiring the original social templates to reload a world.

An observation has a stable ID, person, source, text, direction and timestamp.
A dossier fact has a category, field, value, observed/reported/inferred type,
confidence, timestamp and evidence_ids. Contradictory values are retained and
shown in Unknowns; one source does not silently overwrite another.

Relationship direction is explicit. Known relationship records retain their
supporting observations. Undiscovered people/relationships are not rendered.
The graph and dossier read exclusively from knowledge records.

## Persistent character story values

The office catalog loads Mark from mark.toml (stable ID engineer). His
state_definitions table owns trust and suspicion. assistant.toml defines the
AI character self:assistant with sentience, autonomy and integer restarts.
Each field has a finite initial value and optional bounds. Saved values live
in the owning character's state table, with the AI under ai_character.state.
Conversation rules refer to engineer.trust or self:assistant.sentience; they
share the same values across conversations. These authored scores remain
separate from observed dossier estimates and the workday's task trust.
Defaults initialize missing fields once. Existing values and bounds are saved;
editing a default does not overwrite a running world's values. The previous
Day 1 conversation variables migrate automatically while keeping progress.
