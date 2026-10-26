#!/usr/bin/env bash
# Prepare assets and instructions only. This script NEVER creates a release or tag.
set -euo pipefail
repo=AresUser1/KoteModules
workflow=CI
prefix=kote-modules-android-arm64-
run_id="${1:?Укажите ID успешной сборки}"
tag="${2:?Укажите будущий тег версии}"
[[ "$run_id" =~ ^[0-9]+$ && "$tag" =~ ^v[0-9][0-9A-Za-z._-]*$ ]] || exit 2
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/release-preparation/$tag/$run_id"
[[ ! -e "$out" ]] || { echo "Каталог уже существует: $out" >&2; exit 1; }
mkdir -p "$out/assets"
gh run view "$run_id" --repo "$repo" \
    --json status,conclusion,event,headSha,workflowName > "$out/run.json"
sha="$(python3 - "$out/run.json" "$workflow" <<'CHECK'
import json, sys
run = json.load(open(sys.argv[1]))
if run['status'] != 'completed' or run['conclusion'] != 'success':
    raise SystemExit('Нужна завершённая успешная сборка.')
if run['workflowName'] != sys.argv[2] or run['event'] not in {'push', 'workflow_dispatch'}:
    raise SystemExit('Нужна сборка из доверенной ветки, а не pull_request.')
print(run['headSha'])
CHECK
)"
gh run download "$run_id" --repo "$repo" --name "$prefix$sha" --dir "$out/assets"
python3 - "$out/assets" "$sha" "$repo" "$tag" <<'VERIFY'
from pathlib import Path
import hashlib, json, sys
root = Path(sys.argv[1]).resolve()
for line in (root/'SHA256SUMS').read_text().splitlines():
    expected, name = line.split('  ', 1)
    path = (root/name).resolve()
    if not path.is_relative_to(root) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit('Не совпала контрольная сумма артефакта.')
info = json.loads((root/'build-info.json').read_text())
if info['commit'] != sys.argv[2] or info['repository'] != sys.argv[3]:
    raise SystemExit('Артефакт относится к другой ревизии/репозиторию.')
if info.get('variant') == 'debug':
    raise SystemExit('Debug APK не подходит для релиза.')
if 'version_name' in info and sys.argv[4] != 'v' + info['version_name']:
    raise SystemExit('Тег не совпадает с versionName APK.')
VERIFY
python3 - "$out" "$repo" "$tag" "$sha" <<'NOTES'
from pathlib import Path
import shlex, sys
out, repo, tag, sha = Path(sys.argv[1]), *sys.argv[2:]
notes = f"Version: {tag}\nRepository: {repo}\nCommit: {sha}\n\nПроверить APK/модули на устройстве перед публикацией.\n"
(out/'NOTES.md').write_text(notes)
command = ['gh', 'release', 'create', tag, '--repo', repo, '--target', sha, '--draft', '--title', tag, '--notes-file', str(out/'NOTES.md')]
files = [p for p in sorted((out/'assets').rglob('*')) if p.is_file()]
command.extend(str(p) for p in files)
(out/'REVIEW_BEFORE_RELEASE.txt').write_text(
    'Релиз и тег НЕ созданы. После подтверждения пользователя можно создать черновик:\n' +
    shlex.join(command) + '\n\nПубликация черновика требует отдельного подтверждения пользователя.\n')
NOTES
printf 'Подготовлены файлы для проверки: %s\nРелиз и тег не создавались.\n' "$out"
