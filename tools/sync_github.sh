#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
branch="$(git symbolic-ref --quiet --short HEAD)"
case "$branch" in main|master) echo 'Отправляйте рабочую ветку и создавайте PR.' >&2; exit 1 ;; esac
[[ -z "$(git status --porcelain)" ]] || {
    echo 'Сначала сохраните нужные изменения коммитом. Скрипт не удаляет и не прячет локальные файлы.' >&2
    exit 1
}
git fetch origin
if git show-ref --verify --quiet "refs/remotes/origin/$branch"; then
    git merge --no-edit "origin/$branch"
fi
git merge --no-edit origin/main
git -c core.hooksPath=.githooks push --set-upstream origin "HEAD:refs/heads/$branch"
