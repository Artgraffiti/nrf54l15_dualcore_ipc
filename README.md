# nrf54l15_dualcore_ipc

`nrf54l15_dualcore_ipc` это демонстрационный проект для `nRF54L15`, который показывает
обмен сообщениями между двумя ядрами SoC:

- `CPUAPP` (`Cortex-M33`) запускает benchmark и печатает результаты в UART;
- `CPUFLPR` (`RISC-V`) принимает сообщения от `CPUAPP` и сразу отправляет ответ;
- ядра обмениваются сообщениями через `ipc_service + icmsg`.

Проект показывает режим измерения максимальной частоты обмена:

1. `CPUAPP` формирует сообщение фиксированного размера.
2. `CPUAPP` отправляет его на `CPUFLPR`.
3. `CPUFLPR` сразу возвращает ответ с тем же payload и вычисленным результатом.
4. `CPUAPP` немедленно отправляет следующий запрос.
5. После заданного числа итераций `CPUAPP` печатает статистику:
   `min/avg/max RTT`, `exchange frequency`, `message rate`, `data rate`.

Это полезно как минимальный рабочий пример для:

- запуска двух образов через `sysbuild`;
- связи `CPUAPP <-> CPUFLPR`;
- настройки `ipc_service` на `nRF54L15`;
- оценки предельной частоты IPC-обмена на реальной плате.

## Схема работы

```text
                         nrf54l15_dualcore_ipc

                  +-------------------------------+
                  |         nRF54L15 SoC          |
                  |                               |
                  |   +-----------------------+   |
ping ----------->|   | CPUAPP (Cortex-M33)   |   |
                  |   |-----------------------|   |
                  |   | tight ping-pong loop  |   |
                  |   | ipc_service client    |   |
                  |   | UART logs             |---+--> ttyACM (115200)
                  |   +-----------+-----------+   |
                  |               |               |
                  |      icmsg / ipc_service      |
                  |               |               |
                  |   +-----------v-----------+   |
                  |   | CPUFLPR (RISC-V)      |   |
                  |   |-----------------------|   |
                  |   | receives message      |   |
                  |   | validates payload     |   |
                  |   | computes x^2          |   |
                  |   | sends response back   |   |
                  |   +-----------------------+   |
                  +-------------------------------+

Message flow:

  CPUAPP                              CPUFLPR
    |                                     |
    | send ping {seq, payload}            |
    |------------------------------------>|
    |                                     |
    |                         echo result |
    |<------------------------------------|
    | recv response                       |
    | update RTT stats                    |
    |                                     |
```

## Подготовка окружения

Сборку и прошивку выполняйте после активации виртуального окружения:

```fish
source ~/NCS-Project/.venv/bin/activate.fish
```

Проверка инструментов:

```fish
which west
which python3
west --version
```

Ожидаемо `west` и `python3` должны резолвиться из `~/NCS-Project/.venv`.

## Установка toolchain для CPUFLPR

Для сборки remote-образа `nrf54l15_connectkit/nrf54l15/cpuflpr` нужен
`riscv64-zephyr-elf-gcc`.

Этот компилятор ставится как часть Zephyr SDK.

Проверка текущего состояния SDK:

```fish
source ~/NCS-Project/.venv/bin/activate.fish
cd ~/NCS-Project/zephyr
west sdk list
```

Если в `installed-toolchains` нет `riscv64-zephyr-elf`, его нужно доустановить:

```fish
source ~/NCS-Project/.venv/bin/activate.fish
cd ~/NCS-Project/zephyr
west sdk install --version 0.17.1 --toolchains riscv64-zephyr-elf
```

Если хотите явно держать и ARM, и RISC-V toolchain:

```fish
west sdk install --version 0.17.1 --toolchains arm-zephyr-eabi riscv64-zephyr-elf
```

Проверка после установки:

```fish
west sdk list
ls ~/zephyr-sdk-0.17.1/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gcc
~/zephyr-sdk-0.17.1/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gcc --version
```

Если SDK установлен частично или повреждён, проще переустановить его целиком:

```fish
source ~/NCS-Project/.venv/bin/activate.fish
cd ~/NCS-Project/zephyr
west sdk install --version 0.17.1
```

## Сборка

Проект собирается через `sysbuild` и включает два образа:

- `CPUAPP` для `nrf54l15_connectkit/nrf54l15/cpuapp`
- `CPUFLPR` для `nrf54l15_connectkit/nrf54l15/cpuflpr`

Сборка из каталога проекта:

```fish
cd ~/NCS-Project/OwnApps/nrf54l15_dualcore_ipc
west build \
  -p always \
  -b nrf54l15_connectkit/nrf54l15/cpuapp \
  --sysbuild \
  .
```

Конфигурация запуска `CPUFLPR` добавлена в overlay проекта. Отдельный
`nordic-flpr` snippet здесь не используется, чтобы не забирать UART у `CPUAPP`.

По умолчанию `west build` использует каталог `build/`, поэтому флаг `-d` здесь не нужен.
Его имеет смысл добавлять только если вы хотите собирать в нестандартную директорию.

Основной каталог сборки:

```text
build
```

Полезные подкаталоги:

```text
build/remote
build/nrf54l15_dualcore_ipc
build/domains.yaml
```

Повторная сборка без очистки:

```fish
west build
```

Полностью чистая пересборка:

```fish
west build -p always -b nrf54l15_connectkit/nrf54l15/cpuapp --sysbuild .
```

## Прошивка

Этот проект собирается через `sysbuild`, поэтому прошивать нужно **оба домена**:

1. `remote` - образ для `CPUFLPR`
2. `nrf54l15_dualcore_ipc` - образ для `CPUAPP`

Практически это выглядит так:

```fish
source ~/NCS-Project/.venv/bin/activate.fish
west flash -d build --domain remote --skip-rebuild --target nrf54l
west flash -d build --domain nrf54l15_dualcore_ipc --skip-rebuild --target nrf54l
```

## Настройка benchmark

Основные параметры задаются через Kconfig в `prj.conf` и `remote/prj.conf`:

- `CONFIG_DEMO_PAYLOAD_SIZE` - размер payload в каждом сообщении
- `CONFIG_DEMO_JOB_COUNT` - число round-trip итераций в одном прогоне
- `CONFIG_DEMO_PROGRESS_INTERVAL` - как часто печатать промежуточный прогресс

Для поиска максимальной частоты обмена обычно начинают так:

- `CONFIG_DEMO_PAYLOAD_SIZE=32`
- `CONFIG_DEMO_JOB_COUNT=10000`
- `CONFIG_DEMO_PROGRESS_INTERVAL=1000`

Потом увеличивают `CONFIG_DEMO_PAYLOAD_SIZE` и смотрят, как меняются:

- `Exchange frequency`
- `Message rate`
- `Data rate`

Почему именно так:

- `west flash -d build/nrf54l15_dualcore_ipc` прошивает только `CPUAPP`
- `CPUFLPR` при этом остаётся со старой прошивкой
- это выглядит так, будто "ничего не поменялось", хотя main-образ уже обновился

Если нужен полный erase перед перепрошивкой:

```fish
west flash -d build/nrf54l15_dualcore_ipc --erase --skip-rebuild
west flash -d build --domain remote --skip-rebuild --target nrf54l
```

Если подключено несколько отладчиков, добавляйте `--dev-id` к каждой команде:

```fish
west flash -d build --domain remote --skip-rebuild --target nrf54l --dev-id <probe-id>
west flash -d build --domain nrf54l15_dualcore_ipc --skip-rebuild --target nrf54l --dev-id <probe-id>
```

## Монитор порта

У `nrf54l15_connectkit` Interface MCU поднимает **два USB CDC ACM порта**:

- основной COM-порт: UART bridge между хостом и `nRF54L15`
- второй COM-порт: Interface Shell

Для логов приложения нужен **основной COM-порт**.

Если вы открыли не тот порт, обычно увидите prompt вида:

```text
ifsh:~$
```

Это Interface Shell, а не UART логов приложения.

Поиск портов в Linux:

```fish
ls /dev/ttyACM*
```

Пример подключения через `screen`:

```fish
screen /dev/ttyACM0 115200
```

Пример через `minicom`:

```fish
minicom -D /dev/ttyACM0 -b 115200
```

Параметры UART:

- скорость: `115200`
- 8 data bits
- no parity
- 1 stop bit

Если выдернули кабель или терминал завис, выйти из `screen` можно так:

```text
Ctrl-A, затем K
```

Для `minicom` обычно выход:

```text
Ctrl-A, затем X
```

## Что видно в логах CPUAPP

Пример последовательности:

```text
nrf54l15_dualcore_ipc started
IPC ping-pong benchmark: payload=32 bytes, message=56 bytes, iterations=10000
IPC endpoint bound, CPUFLPR image is reachable
Progress: 1000/10000 round trips, average RTT 18 us
Progress: 2000/10000 round trips, average RTT 18 us

Benchmark completed.
Round trips: 10000
Total time: 181234 us
RTT min/avg/max: 14700 ns / 18100 ns / 35600 ns
Exchange frequency: 55177.42 Hz
Message rate: 110354.84 msg/s
Data rate: 6179871.31 B/s
```

## Логи CPUFLPR

В текущей версии проекта вывод `CPUFLPR` в UART отключён специально, чтобы не конфликтовать с `CPUAPP` на общем UART платы.
В монитор порта должны приходить только строки `CPUAPP`.

Если понадобится, логи `CPUFLPR` можно вернуть отдельной конфигурацией.

## Типовые проблемы

### 1. Сборка запущена без активации `.venv`

Симптомы:

- используется системный `python`
- `west` берётся не из `~/NCS-Project/.venv`
- `sysbuild` падает на Python-зависимостях

Решение:

```fish
source ~/NCS-Project/.venv/bin/activate.fish
```

### 2. Нет RISC-V toolchain для `CPUFLPR`

Симптом:

```text
riscv64-zephyr-elf-gcc not found
```

Это значит, что в Zephyr SDK отсутствует toolchain для сборки remote-образа `cpuflpr`.

### 3. Открыт не тот COM-порт

Симптом:

```text
ifsh:~$
```

Это Interface Shell. Переключитесь на другой `ttyACM` порт.

## Минимальная последовательность

```fish
cd ~/NCS-Project/OwnApps/nrf54l15_dualcore_ipc
source ~/NCS-Project/.venv/bin/activate.fish
west build -p always -b nrf54l15_connectkit/nrf54l15/cpuapp --sysbuild .
west flash -d build --domain remote --skip-rebuild --target nrf54l
west flash -d build --domain nrf54l15_dualcore_ipc --skip-rebuild --target nrf54l
screen /dev/ttyACM0 115200
```
