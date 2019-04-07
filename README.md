Apostol Web Service
=

Веб-сервис **`Апостол`**, исходные коды на C++.

СТРУКТУРА КАТАЛОГОВ
-

    + conf/             содержит файлы с настройками
    + src/              содержит файлы с исходным кодом
      + apostol/        содержит файлы с исходным кодом: Apostol Web Service
      + lib/            содержит файлы с исходным кодом библиотек
      | + delphi/       содержит файлы с исходным кодом библиотеки: Delphi classes for C++
      + mod/            содержит файлы с исходным кодом дополнений (модификаций)
        + WebServer/    содержит файлы с исходным кодом дополнения: Web-Server
        + Client365/    содержит файлы с исходным кодом дополнения: Клиент 365

ОПИСАНИЕ
-

`Apostol Web Service` (`AWS`) - платформа, написанная на C++, для быстрого создания **`RESTful API`** 
сервисов под Linux, со встроенной поддержкой СУБД [PostgreSQL](https://www.postgresql.org/).

Платформа `AWS` построена на базе библиотеки **`libdelphi`** с применением **асинхронной** модели программирования.

Бинарный файл `apostol` - приложение `Linux демон` в задачу которого входит запуск и поддержка работоспособности
двух служб:

~~~
1. HTTP Server;
2. PostgreSQL Server.
~~~

###### При реализации "демона" **apostol**, вдохновение черпалось из исходных кодов [nginx](http://nginx.org). Поэтому управление **apostol** схоже с [управлением nginx](http://nginx.org/ru/docs/control.html#reconfiguration), как следствие производительность и отказоустойчивость **apostol** на уровне [nginx](http://nginx.org).

## HTTP Server

### **Асинхронный HTTP сервер**.

Принимает HTTP запросы от клиентов и в зависимости от значения в заголовке `User-Agent` распределяет их между модулями 
(модификациями). При отсутствии заголовка `User-Agent` или должного, в нём, значения направляет запрос в модуль 
Web-Server превращая `AWS` в `Веб-сервер`.

## PostgreSQL Server

### **Асинхронный PostgreSQL сервер**.
	
Сервер создаёт пул подключений к СУБД Postgres и позволяет отправлять SQL запросы в **`асинхронном`** режиме.
Указать диапазон минимального и максимального количества подключений к СУБД можно в файле конфигурации в секции `[postgres]`

###### Сервер построен на базе библиотеки *`libpq`* с применением асинхронных команд обработки.
	
УСТАНОВКА
-


СБОРКА
-


ЗАПУСК
-

### **Управление apostol**.

Управлять **`apostol`** можно с помощью сигналов.
Номер главного процесса по умолчанию записывается в файл `/usr/local/apostol/logs/apostol.pid`. 
Изменить имя этого файла можно при конфигурации сборки или же в `apostol.conf` секция `[daemon]` ключ `pid`. 

Главный процесс поддерживает следующие сигналы:

|Сигнал   |Действие          |
|---------|------------------|
|TERM, INT|быстрое завершение|
|QUIT     |плавное завершение|
|HUP	  |изменение конфигурации, запуск новых рабочих процессов с новой конфигурацией, плавное завершение старых рабочих процессов|
|WINCH    |плавное завершение рабочих процессов|	

Управлять рабочими процессами по отдельности не нужно. Тем не менее, они тоже поддерживают некоторые сигналы:

|Сигнал   |Действие          |
|---------|------------------|
|TERM, INT|быстрое завершение|
|QUIT	  |плавное завершение|
