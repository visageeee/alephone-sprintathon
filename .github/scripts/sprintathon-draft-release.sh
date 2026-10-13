#!/usr/bin/env bash
set -euo pipefail
: "${RELEASE_TAG:?}" "${GH_REPO:?}" "${GITHUB_SHA:?}"
[[ "$RELEASE_TAG" =~ ^sprintathon-[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || {
  echo 'Use a tag starting with sprintathon- followed by letters, digits, dots, underscores or hyphens.' >&2
  exit 1
}
git check-ref-format "refs/tags/$RELEASE_TAG"

# Fail closed on API errors; a published release must never be overwritten.
releases=$(gh api --paginate "repos/$GH_REPO/releases?per_page=100" --jq '.[] | [.tag_name, .draft, .target_commitish] | @tsv')
existing=$(printf '%s\n' "$releases" | awk -F '\t' -v tag="$RELEASE_TAG" '$1 == tag {print $2 "\t" $3}')
if [[ -n "$existing" ]]; then
  [[ "$existing" == $'true\t'"$GITHUB_SHA" ]] || {
    echo 'This tag already belongs to a published release or a draft from another commit. Use a new tag.' >&2
    exit 1
  }
fi
# Reject an existing tag pointing elsewhere, including annotated tags.
remote_tag=$(git ls-remote origin "refs/tags/$RELEASE_TAG" "refs/tags/$RELEASE_TAG^{}")
if [[ -n "$remote_tag" ]]; then
  tag_commit=$(printf '%s\n' "$remote_tag" | tail -n 1 | cut -f 1)
  [[ "$tag_commit" == "$GITHUB_SHA" ]] || { echo 'Tag points to a different commit.' >&2; exit 1; }
fi
[[ "${1:-}" == check ]] && exit 0
[[ "${1:-}" == upload ]] || { echo 'Expected check or upload' >&2; exit 1; }

assets=(Sprintathon-Windows-x86_64.zip Sprintathon-macOS-universal.zip Sprintathon-Linux-x86_64.flatpak)
for asset in "${assets[@]}"; do test -s "release-assets/$asset"; done
(cd release-assets && sha256sum "${assets[@]}" > SHA256SUMS.txt)
cat > release-assets/BUILD.txt <<BUILD
Repository: $GH_REPO
Commit: $GITHUB_SHA
Tag: $RELEASE_TAG
Workflow: $GITHUB_SERVER_URL/$GH_REPO/actions/runs/$GITHUB_RUN_ID
BUILD
cat > "$RUNNER_TEMP/sprintathon-release-notes.md" <<NOTES
Sprintathon build from commit $GITHUB_SHA.

- Windows: x86_64 ZIP
- macOS: universal Intel / Apple Silicon ZIP (ad-hoc signed; not notarized)
- Linux: x86_64 Flatpak

Game scenario files are supplied separately. Verify downloads using SHA256SUMS.txt.

Draft: test all three packages and edit these notes before publishing.
NOTES
if [[ -z "$existing" ]]; then
  gh release create "$RELEASE_TAG" --draft --target "$GITHUB_SHA" \
    --title "Sprintathon $RELEASE_TAG" --notes-file "$RUNNER_TEMP/sprintathon-release-notes.md"
fi
# Reruns replace assets only in a matching draft; edited release notes are retained.
gh release upload "$RELEASE_TAG" \
  "release-assets/${assets[0]}" "release-assets/${assets[1]}" "release-assets/${assets[2]}" \
  release-assets/SHA256SUMS.txt release-assets/BUILD.txt --clobber
printf 'Draft release prepared: %s\n' "$RELEASE_TAG" >> "$GITHUB_STEP_SUMMARY"
