# Contributing

[English version](CONTRIBUTING.md)

## Перед отправкой изменений

```bash
python3 -m unittest discover -s tests -v
python3 tools/check_public_tree.py
node --check release-build/build-carrier.js
```

Для изменений runtime дополнительно используйте `clang -fsyntax-only` с
определениями релизной сборки, если установлен PS5 Payload SDK.

## Данные консоли

Не добавляйте в issue или pull request `summary.json`, `probe-events.jsonl`,
`tracker-state*.bin`, backup и скриншоты с IP/именами профилей. Используйте
синтетические fixtures. Личные snapshots должны оставаться вне репозитория.

## Изменения установщика

Проверяйте оба режима автозапуска, повторную установку поверх предыдущей
версии и поведение при повреждённом или отсутствующем carrier. Не меняйте
Title ID, порт или пути данных без отдельного migration-плана.
