# CI нативных модулей

Процесс целиком описан в `AresUser1/Kotogram`, `docs/DEVELOPMENT.md`.
Этот репозиторий содержит самостоятельный `.github/workflows/ci.yml`.

`ci/kotogram-ref` закрепляет полный SHA совместимого ядра и SDK. Сначала отправьте
эту ревизию Kotogram на GitHub, затем ветку KoteModules. При обновлении ядра
измените pin и повторите интеграционные тесты.

```bash
export KOTO_CORE_DIR=/путь/к/Kotogram/kotogram-lib
export KOTO_SDK_INCLUDE=/путь/к/Kotogram/kote_module_sdk/include
bash tools/check.sh
```

Тесты: шахматы и настоящая ABI/MTProto-интеграция administration, включая отказ
загрузки на ядре 59. Administration 1.2.0 требует **исправленное ядро 60+**.

Сборка всех Android-модулей (после установки NDK):

```bash
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/26.3.11579264"
bash tools/build_all_android.sh
```

Результат: `dist/android-arm64-v8a/`: `.so`, `index.json`, `build-info.json`,
`SHA256SUMS`. Исходные `modules/` и `index.json` эта команда не перезаписывает.
URLs в каталоге сохраняют канал установки из main; помещение этих файлов
в GitHub Release само по себе не обновляет каталог main или зеркало LoaderKote.

CI запускает тесты перед NDK-сборкой, загружает результат в Actions Artifacts
на 30 дней и не коммитит бинарники автоматически. Для приватного Kotogram нужен
secret `KOTOGRAM_READ_TOKEN` с Contents: read для репозитория ядра.

Безопасная синхронизация (после коммита нужных файлов):

```bash
bash tools/sync_github.sh
```

Скрипт делает fetch, обычные merge и push текущей рабочей ветки, обязательно
запускает pre-push tests. При конфликте останавливается. Локальные файлы не удаляет,
не использует force-push и не удаляет ветки.

Подготовка файлов для будущего релиза:

```bash
bash tools/prepare_release.sh RUN_ID v1.2.0
```

Принимаются только успешные CI-сборки push/workflow_dispatch. Проверяются
репозиторий, commit и контрольные суммы. В `release-preparation/` сохраняются
файлы, заметки и команда для просмотра. Скрипт не создаёт тег или релиз.
Любая публикация требует отдельного подтверждения автора.
