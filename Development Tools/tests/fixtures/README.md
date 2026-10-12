# Historical regression fixtures

`legacy-game/data` preserves older authored definitions required by tests of
migration, mail workflows, delegation, character knowledge and permissions.
CMake passes this fixture root explicitly to those tests. The game itself uses
the root `data/` catalog, so these retired conversations never appear in play.

Keep fixture data stable unless updating a specific compatibility test. Player
saves are never copied into fixtures or into terminal smoke-test environments.
