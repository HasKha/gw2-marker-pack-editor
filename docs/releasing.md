# Releasing

The released `Markus.dll` is the one GitHub Actions builds from the tagged commit, never a local build:
anyone can see which commit and workflow made it, and check it with
`gh attestation verify Markus.dll --repo HasKha/gw2-markus`.

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds Release and runs the tests on every
push to `main` and every pull request. The DLL from each run is downloadable from the run's page as the
`Markus` artifact. A pushed tag `vMAJOR.MINOR.BUILD` also drafts the release with that DLL attached.

## Making a release

1. Set `VERSION_MAJOR`, `VERSION_MINOR` and `VERSION_BUILD` in `CMakeLists.txt`, commit, and push to `main`.
   Nexus offers an update only when the tag is greater than the version compiled in, so the two must
   agree. The workflow fails on a tag that doesn't.
2. Tag that commit and push the tag:

   ```bash
   git tag v2.5.0
   git push origin v2.5.0
   ```

3. When the run under **Actions** is green, open **Releases**. The draft for the tag is there with
   `Markus.dll` and notes generated from the commits since the last tag. Edit the notes and press
   **Publish release**. Players see nothing until then.

A release made on the website first, with a new tag, works too: the tag it pushes starts the same run,
which attaches the DLL to that release instead of drafting one. But the release is public before the DLL
arrives, and Nexus may see it with nothing to download for a few minutes. Make it a draft, or use the
steps above.

## When it goes wrong

- **The tag check failed**: the tag and `CMakeLists.txt` disagree. Delete the tag
  (`git push origin :refs/tags/v2.5.0`, then `git tag -d v2.5.0`), fix the version, and tag again.
- **The build or tests failed on a tag**: no release is drafted. Fix it on `main`, delete the tag the same
  way, and tag the fixed commit.
- **A rerun after the draft exists**: re-running the workflow from the Actions page replaces the DLL on
  the release that is already there.

## What CI does not test

The runner has no marker pack corpus ([testing.md](testing.md#the-corpus)). Cases that need one are
excluded by name (`--exclude-regex corpus`), and the ones that skip without it skip. So a green run does
not cover the corpus sweeps. Run the full suite locally before tagging.
