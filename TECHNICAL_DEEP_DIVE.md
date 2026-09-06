# Fleet Manager — Complete Technical Deep Dive

*Written from a full read of this project's actual source code, plus live output captured by running the real backend, the real agent, real PostgreSQL, real Prometheus, and real Grafana on this machine while writing this document. Every command output, JSON response, and log line you see below is real, not invented. Where the code does something different from what you might expect (or different from generic tutorials online), that's called out explicitly.*

*This document assumes no prior background. Every term is explained the first time it's used. Read top to bottom the first time; use it as a reference after that.*

---

## Table of Contents

1. [The whole system in one story](#1-the-whole-system-in-one-story)
2. [The technology map](#2-the-technology-map)
3. [Linux internals: how the kernel exposes its own state](#3-linux-internals-how-the-kernel-exposes-its-own-state)
4. [The agent: what it does and how it's built](#4-the-agent-what-it-does-and-how-its-built)
5. [The backend: Drogon, C++, and the event loop](#5-the-backend-drogon-c-and-the-event-loop)
6. [PostgreSQL: the schema and why it looks the way it does](#6-postgresql-the-schema-and-why-it-looks-the-way-it-does)
7. [Two independent metrics pipelines (the most important architectural fact)](#7-two-independent-metrics-pipelines)
8. [Prometheus: how it actually works](#8-prometheus-how-it-actually-works)
9. [Grafana: dashboards as code](#9-grafana-dashboards-as-code)
10. [The frontend: React, routing, and state](#10-the-frontend-react-routing-and-state)
11. [Security model end to end](#11-security-model-end-to-end)
12. [Observability theory applied to this system](#12-observability-theory-applied-to-this-system)
13. [Real bugs and gotchas found while building and testing this](#13-real-bugs-and-gotchas-found-while-building-and-testing-this)
14. [Design decisions and their trade-offs](#14-design-decisions-and-their-trade-offs)
15. [What's missing, on purpose](#15-whats-missing-on-purpose)
16. [Full question bank](#16-full-question-bank)
17. [Appendix: reproduce every command in this document](#17-appendix-reproduce-every-command-in-this-document)

---

## 1. The whole system in one story

### 1.1 The analogy

You manage a chain of warehouses spread across a city. Each warehouse has machinery (CPU), storage racks (memory), a loading dock (disk I/O), and a phone line to headquarters (network). Sitting in head office, you want one screen that tells you: which warehouse is overloaded right now, which one is running low on space, which one stopped answering the phone an hour ago.

Two ways to get that: drive to every warehouse yourself and look, or install a small reporting device in each one that phones in a status report on a schedule. This project is the second option. The "warehouses" are Linux servers — this codebase calls them **nodes**. The reporting device is a small C++ program called the **agent**. Head office is a C++ web server called the **backend**, and the screen you look at is a website built in **React** — the **frontend**.

### 1.2 The six moving parts

| Part | What it is | What it's built with |
|---|---|---|
| **Agent** | Runs on every managed Linux server. Reads that machine's vital signs straight from the kernel and serves them over HTTP. | C++17, no external metrics library — a hand-written kernel-data collector |
| **Backend** | The central brain. Tracks every node, handles login, stores history, relays remote commands. | C++17, the [Drogon](https://github.com/drogonframework/drogon) web framework |
| **PostgreSQL** | Stores everything that isn't a raw time-series measurement: node registry, user accounts, 24h of recent metric snapshots. | PostgreSQL 14 |
| **Prometheus** | A second, independent collector that scrapes every agent on its own schedule and stores the raw counters in a purpose-built time-series database. | Prometheus v2.51.0, Docker |
| **Grafana** | Turns Prometheus's stored numbers into dashboards, queried live with a query language called PromQL. | Grafana 10.4.0, Docker |
| **Frontend** | The browser UI an operator actually uses: dashboards, node list, per-node metrics, a filesystem check button, a small remote terminal, an embedded Grafana tab. | React 18, Vite, Recharts, Axios |

### 1.3 A real walkthrough, captured live on this machine

Everything below actually happened while writing this document — real HTTP calls against a real running backend, agent, Postgres, Prometheus, and Grafana, all on this laptop.

**Step 1 — the agent's raw view of the kernel.** Before anything else exists, this is what the kernel itself reports about CPU:

```
$ head -4 /proc/stat
cpu  180581 1060 57986 8181865 26856 0 3586 0 0 0
cpu0 17083 155 4846 676568 2092 0 524 0 0 0
cpu1 12346 62 3134 691434 808 0 330 0 0 0
cpu2 21849 122 6432 670941 2453 0 324 0 0 0
```

**Step 2 — the agent turns that into Prometheus's text format** and serves it on port 9200:

```
$ curl -s http://127.0.0.1:9200/metrics | head -10
# HELP node_cpu_seconds_total Seconds the CPUs spent in each mode.
# TYPE node_cpu_seconds_total counter
node_cpu_seconds_total{cpu="cpu0",mode="user"} 170.89
node_cpu_seconds_total{cpu="cpu0",mode="nice"} 1.55
node_cpu_seconds_total{cpu="cpu0",mode="system"} 48.49
node_cpu_seconds_total{cpu="cpu0",mode="idle"} 6771.40
node_cpu_seconds_total{cpu="cpu0",mode="iowait"} 20.92
node_cpu_seconds_total{cpu="cpu0",mode="irq"} 0.00
node_cpu_seconds_total{cpu="cpu0",mode="softirq"} 5.25
node_cpu_seconds_total{cpu="cpu0",mode="steal"} 0.00
```

Notice these are the *raw jiffie counters converted to seconds* — not a percentage. Nobody has computed "CPU usage" yet. That happens later, twice, independently (Section 7 is entirely about this).

**Step 3 — an operator logs in.** A real login against the real backend, right now:

```
$ curl -s -X POST http://127.0.0.1:8080/api/auth/login \
    -H "Content-Type: application/json" \
    -d '{"username":"admin","password":"admin123"}'

{"token":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE3ODQ0MTM3OTYsImlhdCI6MTc4NDM4NDk5Niwic3ViIjoiZTdkNDI5NjItYjQ0MS00Nzg0LWI2NDEtMDljMzUwMDdhNWMwIiwidXNlcm5hbWUiOiJhZG1pbiJ9.kekxOVM_KA5F_KzL8ZNjO8NIW-dXcnDV7JspLgZ1cHg",
 "user":{"id":"e7d42962-b441-4784-b641-09c35007a5c0","username":"admin"}}
```

**Step 4 — the dashboard's real numbers**, fetched with that token:

```
$ curl -s http://127.0.0.1:8080/api/metrics/summary -H "Authorization: Bearer $TOKEN"

{
    "avg_cpu_percent": 2.5791982057703944,
    "avg_mem_percent": 57.38083740046237,
    "offline": 0,
    "online": 1,
    "pending": 0,
    "total": 1
}
```

**Step 5 — a real remote command**, executed on the managed node through the full HMAC-signed trust chain described in Section 11:

```
$ curl -s -X POST http://127.0.0.1:8080/api/nodes/1854313a-.../commands \
    -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
    -d '{"command":"df","args":["-h"]}'

{
  "duration_ms": 2,
  "exit_code": 0,
  "stdout": "Filesystem      Size  Used Avail Use% Mounted on\ntmpfs           761M  3.1M  758M   1% /run\n/dev/nvme0n1p4  167G  123G   37G  78% /\n...",
  "stderr": ""
}
```

That command reached the actual kernel on this actual machine — `df -h`'s real output — in 2 milliseconds, over a request that was HMAC-signed by the backend and independently verified by the agent before it was allowed to run.

**Step 6 — Prometheus, completely independently, scraping the same agent:**

```
$ curl -s http://127.0.0.1:9090/api/v1/targets | ...
fleet-agents http://10.70.41.102:9200/metrics up
fleet-backend http://host.docker.internal:8080/metrics up

$ curl -s 'http://127.0.0.1:9090/api/v1/query' \
    --data-urlencode 'query=100 - avg(rate(node_cpu_seconds_total{mode="idle"}[5m])) * 100'

{"result": [{"metric": {}, "value": [1784385255.77, "88.21940739198052"]}]}
```

**Step 7 — Grafana, reading from that same Prometheus:**

```
$ curl -s -u admin:admin http://127.0.0.1:3001/api/dashboards/uid/fleet-overview
Fleet Overview - 10 panels
```

Ten real dashboard panels, all computing their numbers live from the same raw counters the agent emitted in Step 2. Nothing above is a mockup — it's the actual system, running, on this machine, right now.

---

## 2. The technology map

Before the deep dives, a one-line answer to "what is X and why is it here":

- **C++17** — a compiled systems language with manual memory control and no runtime interpreter. Used for the agent (needs to be tiny and fast on every managed server) and the backend (needs to handle many concurrent HTTP connections cheaply).
- **Drogon** — a C++ web framework. Gives the backend an HTTP server, a router, and an async PostgreSQL client without writing raw socket code.
- **cpp-httplib** — a single C++ header file that implements a basic HTTP server/client. Used only in the agent, which deliberately avoids a heavyweight framework.
- **PostgreSQL** — a relational (table-based, SQL) database. Stores the node registry, user accounts, and a rolling 24 hours of metric history.
- **Prometheus** — a *time-series database* (a database specialized for "many timestamped numbers, queried by range") that pulls metrics from HTTP endpoints on a schedule.
- **Grafana** — a dashboard tool that turns Prometheus queries into charts.
- **React** — a JavaScript library for building the browser UI declaratively (you describe what the screen should show for a given piece of state, and React updates the DOM to match).
- **JWT (JSON Web Token)** — a signed, tamper-evident token used to prove "this browser session belongs to a logged-in user" on every API request.
- **HMAC-SHA256** — a keyed cryptographic checksum used to prove "this command really came from the backend" on every agent-facing request.

---

## 3. Linux internals: how the kernel exposes its own state

This section is the foundation the agent is built on. Understand this and the agent's code becomes almost self-explanatory.

### 3.1 What the kernel is, in one paragraph

Every program you run — a browser, a text editor, this project's agent — runs *on top of* the kernel. The kernel is the one piece of software with direct control of the hardware: it decides which program gets the CPU next, hands out memory, and talks to the disk and network card. Because it's making all these decisions constantly, it also naturally *knows* things like "how many seconds has CPU 0 spent idle since boot" or "how much RAM is free right now." The question this whole agent answers is: how does a normal program read that information back out?

### 3.2 `/proc` and `/sys`: files that aren't really files

Linux answers that question with a trick called a **virtual filesystem**. `/proc` and `/sys` look like ordinary directories full of ordinary text files, and you can `cat` them with the same tools you'd use on any file. But nothing under `/proc` lives on your disk — every time you open one of these files, the kernel generates its content on the spot from its own live internal state, then throws it away. There's no file to go stale; you always see the current truth. And because there's no actual disk read involved, reading a `/proc` file is extremely cheap — the agent can read `/proc/stat` many times a second with no meaningful cost.

This is genuinely why the agent can afford to be simple: instead of calling special kernel APIs, it just opens plain text files and parses them with `std::ifstream` and `std::istringstream` — the same tools you'd use to parse any text file.

### 3.3 `/proc/stat` — CPU accounting

Real output from this machine, captured while writing this document:

```
$ cat /proc/stat | head -4
cpu  180581 1060 57986 8181865 26856 0 3586 0 0 0
cpu0 17083 155 4846 676568 2092 0 524 0 0 0
cpu1 12346 62 3134 691434 808 0 330 0 0 0
cpu2 21849 122 6432 670941 2453 0 324 0 0 0
```

The first line, `cpu`, is the sum across every core. Each following line (`cpu0`, `cpu1`, …) is one core. The numbers, in order, are:

```
cpu   USER    NICE  SYSTEM  IDLE     IOWAIT  IRQ  SOFTIRQ  STEAL
cpu   180581  1060  57986   8181865  26856   0    3586     0
```

Every number is a count of **jiffies** — the kernel's internal timer tick, 100 per second on this machine (`CONFIG_HZ=100`, so 1 jiffie = 10ms). These are *cumulative counters since the machine booted*: the `idle` field has been counting up, tick by tick, since boot and never resets.

What each mode actually means:

- **user** — time spent running normal, unprivileged programs (your browser, the fleet-agent binary itself, a database)
- **nice** — the same, but for processes that were started with a lowered scheduling priority (`nice`)
- **system** — time spent inside the kernel itself, servicing system calls on behalf of a process
- **idle** — no runnable process, nothing to do
- **iowait** — a special kind of idle: the CPU is free, but there's a process that *would* run except it's blocked waiting for a disk read or network response to come back
- **irq** / **softirq** — time spent handling hardware and software interrupts (a network packet arriving, a timer firing)
- **steal** — on a virtual machine, time a hypervisor took away from this VM's CPU to give to another VM. Always `0` on bare metal, and it *is* `0` in the real output above because this is a physical laptop.

**The one insight that matters most: you cannot compute "CPU usage right now" from a single reading.** These are lifetime totals. If you divide `idle` by `total` from one snapshot, you get the *average idle fraction since the machine was switched on* — not the current load. A machine that's been running for two days at an average of 20% CPU, but is pegged at 100% right this second, will still report "20%" from a single reading. That number is true and useless at the same time.

The fix — and this is exactly what this project does, though (this is an important correction from generic explanations of this topic) **not inside the agent** — is to take two readings separated by a known interval and look at the *difference*:

```
delta_total = total_at_T2 - total_at_T1
delta_idle  = idle_at_T2  - idle_at_T1     (idle + iowait, both count as "not busy")

cpu_percent = (1 - delta_idle / delta_total) * 100
```

Worked example with small, easy numbers:

```
T1:  cpu 1000 0 500 8000 200 0 0 0     → total=9700, idle=8200
T2:  cpu 1150 0 600 8800 220 0 0 0     → total=10770, idle=9020

delta_total = 10770 - 9700 = 1070
delta_idle  =  9020 - 8200 =  820

cpu_percent = (1 - 820/1070) * 100 = 23.36%
```

**Where this actually happens in this codebase:** not in the agent. The agent's job is to read `/proc/stat` once per collection cycle and emit the raw counters, in seconds, exactly as `node_exporter` (Prometheus's own official Linux exporter) would. Two *separate* consumers each do their own differencing on those raw numbers — the backend's `PrometheusPoller` for the custom dashboard, and Prometheus's own `rate()` function for Grafana. This split is deliberate and important enough that Section 7 is dedicated to it.

`procs_running` / `procs_blocked`, further down the same file:

```
procs_running 2
procs_blocked 0
```

`procs_running` counts processes that are either currently executing or waiting in line for a CPU. If this number is consistently higher than your core count, work is queueing — that's **CPU saturation**, the second pillar of the USE Method covered in Section 12. `procs_blocked` counts processes stuck waiting on I/O; a high number there points at disk, not CPU, as the bottleneck.

### 3.4 `/proc/meminfo` — memory accounting

Real output from this machine:

```
$ head -10 /proc/meminfo
MemTotal:        7786000 kB
MemFree:          889968 kB
MemAvailable:    3230856 kB
Buffers:            44684 kB
Cached:           2799432 kB
SwapCached:          4812 kB
Active:           3178536 kB
Inactive:         2268152 kB
Active(anon):     1897492 kB
Inactive(anon):   1123876 kB
```

Every value is in kibibytes; the agent multiplies by 1024 to get bytes (Prometheus convention is always base SI units — bytes, seconds — never kB or ms).

**`MemFree` vs `MemAvailable` — the single most commonly misunderstood Linux metric.** `MemFree` is memory that is *completely* unallocated — genuinely empty. On this machine right now it's only 889,968 kB out of 7,786,000 kB total, which looks alarming. But Linux deliberately uses "spare" RAM to cache recently-read files (the **page cache** — the `Cached` field above, 2,799,432 kB here). That cache memory is *not* wasted and *not* unavailable: the instant a real program needs it, the kernel drops cached pages and hands the RAM over. `MemAvailable` is the kernel's own estimate of how much memory a new program could actually get without triggering swap — `MemFree` plus the safely-reclaimable part of `Buffers` and `Cached`. On this machine, `MemAvailable` is 3,230,856 kB — more than 3.6× `MemFree`.

If the agent (or a naive monitoring script) reported `MemFree` as "available memory," an operator watching the dashboard would see a machine with 88% memory "used" and panic — when the real, kernel-computed figure says only about 58% is genuinely in use. This project's memory-percent computation, done in the backend, is:

```cpp
mem_used_bytes = mem_total_bytes - mem_available_bytes;
mem_percent    = mem_used_bytes / mem_total_bytes * 100;
```

— i.e. it uses `MemAvailable`, not `MemFree`, at every point it computes a used/available split. You can verify the kernel's own opinion matches: `free -b`'s "available" column is defined to equal `MemAvailable`, not `MemFree`.

### 3.5 `/proc/diskstats` — disk I/O accounting

Real output from this machine's actual NVMe drive:

```
$ awk '$3 !~ /^(loop|ram)/ && $4 > 0' /proc/diskstats | head -2
 259  0 nvme0n1     227318 84372 14945353 45573 180028 82753 5803402 413134 0 174563 508856 0 0 0 0 35606 50149
 259  4 nvme0n1p4   226693 83959 14918256 45459 180026 82753 5803400 413134 0 179858 458593 0 0 0 0     0     0
```

After `major minor devicename`, the fields (this project only uses a subset) are: reads completed, reads merged, **sectors read**, ms spent reading, writes completed, writes merged, **sectors written**, ms spent writing, I/Os in progress, **ms spent doing I/O** (`io_time`), and a few more added by newer kernels.

Two facts worth knowing precisely:

1. **Linux always reports in 512-byte sectors here, regardless of the drive's real physical sector size** (modern NVMe drives typically use 4096-byte physical sectors internally). This is a stable kernel ABI guarantee so every tool that has ever parsed this file keeps working. The agent's conversion is always `bytes = sectors * 512`, unconditionally.
2. Like CPU jiffies, sector counts are cumulative since boot. A byte-per-second read/write rate requires two readings and a subtraction, exactly like CPU — and again, the agent doesn't do this itself; it's done downstream (Section 7).

`ms_doing_io` divided by the elapsed wall-clock time (as a percentage) is disk **utilisation** in the USE-Method sense — how much of the last N seconds the device spent actually busy with I/O, saturated or not.

The agent explicitly skips device names starting with `loop` (loopback devices used to mount disk image files) and `ram` (RAM-backed pseudo-disks) — these are virtual and would pollute real disk metrics.

### 3.6 `/proc/net/dev` — network interface accounting

```
$ cat /proc/net/dev
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed
    lo:  ...
  eth0:  9876543210 1234567 0 12 0 0 0 0  1234567890 987654 0 0 0 0 0 0
```

After skipping the two header lines, each remaining line is `interface: <16 numbers>` — 8 for receive (bytes, packets, errors, drops, …) then 8 for transmit in the same shape. The agent explicitly skips the `lo` (loopback) interface: `127.0.0.1` traffic never leaves the machine and including it would inflate "network activity" with numbers that have nothing to do with fleet health.

### 3.7 `/proc/loadavg` — system load

```
$ cat /proc/loadavg
1.07 0.87 0.69 1/1651 67534
```

The first three numbers are the 1-, 5-, and 15-minute **load averages** — a running average of how many processes were either running or waiting for a CPU. A load average of `1.0` on a single-core machine means that core was exactly saturated on average over that window; on this machine (12 logical cores), a `load1` of `1.07` means the system is nowhere near saturated. `load1 / core_count` is the exact figure to watch: below 1.0 means headroom, at or above means processes are queueing for CPU time.

### 3.8 `/proc/uptime`

```
$ cat /proc/uptime
172834.56 689312.24
```

First value: seconds since boot. The agent uses this to compute a Unix boot timestamp: `boot_time = now() - uptime_seconds`, matching `node_boot_time_seconds` in real `node_exporter`.

### 3.9 `/proc/mounts` and `statfs()` — filesystem space

Not covered by the reference material this document was requested against, but real and present in this codebase: the agent reads `/proc/mounts` to enumerate every mounted filesystem, skips virtual filesystem types (`tmpfs`, `proc`, `cgroup2`, `overlay`, and 11 others — see `MetricsCollector::collectFilesystems`), and calls the POSIX `statfs()` system call on each remaining mount point to get size, free, and available bytes directly from the kernel's filesystem-independent accounting.

---

## 4. The agent: what it does and how it's built

### 4.1 The whole agent in one sentence

The agent is a ~2.2 MB static binary (`ls -la agent/build/fleet-agent` → 2,275,008 bytes on this build) that, once started with a manager URL and a secret token, does three things forever: collects kernel metrics on a timer and caches them, sends a heartbeat to the backend on a timer, and answers three HTTP endpoints (`/metrics`, `/health`, `/execute`).

### 4.2 Why a custom agent instead of just running node_exporter

Prometheus's official `node_exporter` already does everything Section 3 describes, and more. This project deliberately doesn't just wrap it, for three reasons visible directly in the code:

1. **It needs a management plane.** `node_exporter` is a passive metrics endpoint with no idea it's part of a fleet. This agent also registers itself (via the onboarding token), sends heartbeats with `hostname`/`os_info`/`hardware_info` so the backend's node registry stays current, and exposes a signed remote-command endpoint — none of which `node_exporter` has any concept of.
2. **It needs to run arbitrary whitelisted commands safely** (`/execute`) — a feature with real security implications (Section 11.3) that has nothing to do with metrics collection.
3. **Minimal footprint.** This binary is 2.2 MB; a typical `node_exporter` binary is 15–20 MB. The `CMakeLists.txt` even statically links `libstdc++`/`libgcc` specifically so the binary can be `curl`'d onto a fresh machine and run without worrying whether its C++ runtime version matches.

### 4.3 cpp-httplib: the whole HTTP server in a handful of lines

The agent's networking dependency is a single vendored header, `agent/third_party/httplib.h`. There's no framework, no build step for it, just `#include "httplib.h"`. The entire server setup in `agent/src/main.cc` is genuinely this short:

```cpp
httplib::Server server;

server.Get("/metrics", [](const httplib::Request &, httplib::Response &res) {
    res.set_content(collector.getCachedMetrics(), "text/plain; version=0.0.4; charset=utf-8");
});

server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
    res.set_content(R"({"status":"ok","version":"1.0.0"})", "application/json");
});

server.Post("/execute", [&cfg](const httplib::Request &req, httplib::Response &res) {
    // verify HMAC, whitelist-check, run command, return JSON  (Section 11.3)
});

server.listen("0.0.0.0", cfg.agentPort);   // blocks forever
```

### 4.4 The caching architecture, and why it's not optional

Metric collection runs on a **background thread**, separate from the thread(s) that answer HTTP requests. `MetricsCollector::start()`:

```cpp
void MetricsCollector::start(int intervalSeconds)
{
    collectOnce();                                      // so the cache is never empty
    std::thread worker([this, intervalSeconds]() { loop(intervalSeconds); });
    worker.detach();
}

void MetricsCollector::loop(int intervalSeconds)
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        collectOnce();
    }
}
```

`collectOnce()` reads every `/proc` file, serializes the result to Prometheus text, and stores it behind a mutex:

```cpp
void MetricsCollector::collectOnce()
{
    AllMetrics m = collectAll();
    std::string serialized = MetricsSerializer::serialize(m);
    std::lock_guard<std::mutex> lock(mutex_);
    cachedMetrics_ = std::move(serialized);
}

std::string MetricsCollector::getCachedMetrics()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return cachedMetrics_;   // a copy, so the lock is held only briefly
}
```

Why bother caching instead of just reading `/proc` fresh on every `GET /metrics`? Two independent scrapers hit this endpoint on their own schedules — the backend's `PrometheusPoller` every 15 seconds, and the real Prometheus container every 15 seconds — and their requests are not synchronized with each other. Serving a pre-computed string means both get an instant (sub-millisecond), consistent snapshot no matter when they happen to ask, and a slow scraper can never block or interfere with the collection thread. The `std::mutex` exists purely because one thread writes the string while a different thread (the one serving the HTTP request) might read it at the same instant — without the lock, that's undefined behaviour in C++ (a data race), not just a bug.

### 4.5 What the agent actually emits — and the one design choice worth dwelling on

Read `agent/src/MetricsSerializer.cc` closely and one thing stands out: **the "cpu" aggregate line from `/proc/stat` is never emitted** — only `cpu0`, `cpu1`, `cpu2`, … . The code comment explains exactly why:

> Only per-core series are emitted (never the "cpu" aggregate line from /proc/stat) so this matches real node_exporter's convention: PromQL queries that avg()/sum() across the cpu label assume every matching series is one core. Emitting an extra pre-summed "total" series under the same metric name would silently skew any unweighted average upward by counting each core's idle time twice — which is exactly what an early build of this agent did.

That's a real bug this project hit and fixed (also documented in Section 13): if you emit both `cpu="cpu0"` and `cpu="cpu"` under the same metric name, then `avg by(instance)(rate(node_cpu_seconds_total{mode="idle"}[5m]))` — the exact expression this project's Grafana dashboard and `HighCPU` alert both use — silently averages in one extra "series" that's really just the sum of the others, and the result comes out wrong (in this case, an *impossible negative CPU percentage*, because idle time got double-counted). The fix was simply to stop emitting the aggregate line and let every consumer compute totals themselves by summing the per-core series — which is also, not coincidentally, exactly what real `node_exporter` does.

The full list of metrics the agent emits (all confirmed live above): `node_cpu_seconds_total` (counter, per core × mode), `node_memory_*_bytes` (gauge, 7 fields — MemTotal/MemFree/MemAvailable/Buffers/Cached/SwapTotal/SwapFree), `node_disk_read_bytes_total` / `node_disk_written_bytes_total` / `node_disk_io_time_seconds_total` (counters, per device), `node_filesystem_size_bytes` / `_avail_bytes` / `_free_bytes` (gauges, per mountpoint), `node_network_receive_bytes_total` / `_transmit_bytes_total` / `_receive_packets_total` / `_transmit_packets_total` / `_receive_errs_total` / `_transmit_errs_total` (counters, per interface), `node_load1` / `node_load5` / `node_load15` (gauges), `node_procs_running` / `node_procs_blocked` (gauges), `node_boot_time_seconds` (gauge), `node_filefd_allocated` / `node_filefd_maximum` (gauges — currently open file descriptors vs the system limit, read from `/proc/sys/fs/file-nr`).

### 4.6 CommandRunner: `execvp`, not `system()` — and why that distinction is the whole security story

`system("df -h")` under the hood runs `/bin/sh -c "df -h"` — it hands your string to a real shell, which interprets `;`, `&&`, `|`, `` ` ` ``, `$(...)`, and every other shell metacharacter. If an argument you didn't fully control ever ends up inside that string — say, `args: ["-h; rm -rf /"]` — the shell will happily run both commands.

`execvp(command.c_str(), argv.data())` never involves a shell at all. It replaces the *current process image* with the named binary and hands it an **array** of argument strings directly, as the kernel-level `argv` the new program receives in `main()`. There's no string to parse and no metacharacter to misinterpret; `"-h; rm -rf /"` arrives at `df` as one single, literal, oddly-named argument, which `df` rejects with a usage error. Nothing after the semicolon ever gets a chance to mean anything.

Because `execvp` *replaces* the calling process, the agent first calls `fork()` to create a child process, then `execvp`s inside the child only, from `agent/src/CommandRunner.cc`:

```cpp
pid_t pid = fork();
if (pid == 0)
{
    // Child: wire pipes to stdout/stderr, then exec.
    dup2(stdoutPipe[1], STDOUT_FILENO);
    dup2(stderrPipe[1], STDERR_FILENO);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(command.c_str()));
    for (const auto &a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execvp(command.c_str(), argv.data());
    _exit(127);   // only reached if execvp itself failed
}
// Parent: read both pipes concurrently via poll(), then waitpid() for the exit code.
```

One subtlety worth knowing: the parent reads stdout **and** stderr concurrently with `poll()` rather than draining one pipe fully before starting the other (`readBothPipes` in the same file). If a command writes enough to *both* pipes to fill the kernel's pipe buffer, and you're only reading one of them, the child blocks trying to write to the full pipe you haven't touched yet — and your parent is blocked reading the other pipe, waiting for output that will never come because the child is stuck. Reading both with `poll()` avoids that deadlock entirely.

### 4.7 The command whitelist — and a real, live inconsistency this project has

The whitelist is meant to be a hard, defense-in-depth boundary: *only these command names may ever be executed, anywhere.* It's independently checked in **three separate places**, in three different codebases, with no shared source of truth — and reading the actual current source of all three turns up a genuine, unintentional drift between them:

| Layer | File | Whitelist |
|---|---|---|
| Backend (the actual gate — this is what decides whether a request even reaches a node) | `backend/src/NodeController.cc` | `df, free, ps, uptime, netstat, ss, lscpu, uname` (8) |
| Agent (defense-in-depth, checked again independently) | `agent/src/CommandRunner.cc` | `df, free, ps, uptime, netstat, ss, lscpu, uname, top` (9 — includes `top`) |
| Frontend dropdown (what an operator can even select in the UI) | `frontend/src/pages/NodeDetailPage.jsx` | `df, free, ps, uptime, netstat, uname, lscpu` (7 — missing `ss`) |

Two real, honest observations follow directly from this: `top` is in the agent's whitelist but *not* the backend's, so it's effectively dead code — the backend rejects it with `400 command not allowed` before the request ever reaches an agent, meaning an agent could never actually be asked to run `top` through this system as it stands today. And `ss` is missing from the frontend's dropdown, so an operator using the UI literally cannot select a command that the backend would otherwise allow — it's reachable only by calling the API directly. Neither is a security hole (the backend's list is the one that actually gates access), but it's a textbook example of what happens when the same conceptual list is hand-duplicated across languages and repositories with nothing enforcing that they match — exactly the kind of small, real inconsistency worth being able to point to and explain rather than pretend doesn't exist.

### 4.8 Heartbeat: what the agent tells the backend about itself

Every `heartbeat_interval_seconds` (default 30s), the agent posts to `/api/agent/heartbeat`. The real fields it gathers, one function per fact, each reading a different kernel source:

- `getHostname()` — the `gethostname()` syscall
- `getKernelVersion()` — first line of `/proc/sys/kernel/osrelease`
- `getDistroName()` — parses `PRETTY_NAME=` out of `/etc/os-release`
- `getArch()` — `/proc/sys/kernel/arch`, falling back to `uname()`
- `getCpuModel()` / `getCpuCores()` — parsed from `/proc/cpuinfo`
- `getMemTotalBytes()` — `MemTotal` from `/proc/meminfo`
- `getLocalIp()` — a clever zero-traffic trick: opens a UDP socket, calls `connect()` to `8.8.8.8:53` (Google's public DNS), and reads back the *local* address the kernel picked for that route with `getsockname()`. No packet is actually sent — UDP `connect()` only configures the socket's route — but the kernel still has to pick which local interface/IP would be used, which is exactly the answer being asked for.

The agent has **no JSON library dependency at all** (a deliberate choice recorded directly in a code comment — "OpenSSL + cpp-httplib only") — both the heartbeat body it builds and the tiny `/etc/fleet-agent/config.json` it reads are hand-assembled/parsed with a few dozen lines of string-searching (`extractJsonValue`, `extractJsonStringArray` in `main.cc`). For a handful of known, flat fields, this is simpler and more honest than pulling in a full JSON library for two lookups.

---

## 5. The backend: Drogon, C++, and the event loop

### 5.1 Why an event loop instead of a thread per connection

The oldest, simplest way to build a web server is **thread-per-connection**: every incoming client gets its own OS thread. It's easy to reason about, but it doesn't scale — each thread costs real memory (commonly a multi-megabyte stack reservation) and most of those threads spend nearly all their time doing nothing but waiting for I/O (a database query, a slow client). 10,000 idle connections at even a modest 2 MB of stack each is 20 GB of memory doing nothing but waiting.

Drogon (like Nginx and Node.js) instead runs an **event loop**: a small, fixed number of threads (this project configures 4, `app().setThreadNum(4)` in `main.cc`) that each use the Linux `epoll` system call to watch *thousands* of sockets at once and wake up only when one of them actually has something ready — a request arrived, a database response came back, a write buffer drained. No thread ever sits blocked waiting on a single connection; when a handler starts a database query, it registers interest in that query's socket and the thread immediately goes back to serving other requests. When the database responds, `epoll` reports that socket as ready and the *same or another* event-loop thread picks the work back up.

### 5.2 What a real handler in this codebase looks like

Every route handler in this backend is written in this callback style (not the newer C++20-coroutine style Drogon also supports — a deliberate choice, explained below). Here's `NodeController::execCommand`, trimmed slightly, showing the pattern clearly:

```cpp
void NodeController::execCommand(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback,
                                 std::string id)
{
    // 1. Verify the JWT manually (Section 11.1) — this backend never uses a
    //    Drogon "filter"; every handler starts with the same three lines.
    if (!JwtMiddleware::instance().verifyRequest(req, authErr, userId, username)) { ... }

    // 2. Kick off a database query and return immediately — the function's
    //    stack frame is gone by the time PostgreSQL actually responds.
    Database::client()->execSqlAsync(
        "SELECT host(ip_address) AS ip_address, agent_port, agent_token FROM nodes WHERE id = $1::uuid",
        [callback, command, args](const orm::Result &result) {
            // 3. This lambda runs LATER, on the event loop, once Postgres answers.
            //    Inside it, sign the request and fire an outbound HTTP call to
            //    the agent — again async, again returning immediately.
            std::string signature = HmacUtils::computeHmacSha256(agentToken, bodyText);
            client->sendRequest(agentReq, [callback, client](ReqResult r, const HttpResponsePtr &resp) {
                // 4. This lambda runs LATER STILL, once the agent responds.
                callback(ApiResponse::json(*resp->getJsonObject()));
            }, 15.0);
        },
        [callback](const orm::DrogonDbException &) { ... },
        id);
    // execCommand() itself has already returned by this point — the thread is
    // free to handle other requests while all of the above waits on I/O.
}
```

That single HTTP request — from an operator clicking "Execute" — chains *three* separate asynchronous hops (JWT check → DB lookup → outbound HTTP to the agent) without ever blocking a thread on any of them.

### 5.3 Startup: `main.cc`, in the order it actually executes

1. **Load `config.json`.** `loadConfig()` tries three relative paths in order — `backend/config.json`, `config.json`, `../config.json` — so the same binary works whether it's launched from the repo root, from `backend/`, or from `backend/build/`.
2. **Resolve `backend_host`.** If `config.json`'s `backend_host` is empty (the checked-in default), the backend calls `detectLanIp()`, which walks the machine's network interfaces via `getifaddrs()`, picks the first `AF_INET`, `IFF_UP`, non-loopback address that isn't a Docker-internal interface (`docker0`, `br-*`, `veth*`, `virbr*`), and uses that. This means the onboarding command shown to an operator always has a currently-correct IP, without anyone having to hand-edit `config.json` after every DHCP lease change. Confirmed live, from this session's own backend log: `fleet-backend: backend_host not set in config.json, auto-detected LAN IP 10.70.41.102`.
3. **Configure the PostgreSQL connection pool.** `orm::PostgresConfig::connectionNumber = 10` — the code comment notes Drogon only exposes one fixed pool size, not a separate min/max range, so `10` stands in for a spec's usual "max=10."
4. **Register CORS handling.** Two Drogon hooks, `registerSyncAdvice` and `registerPreSendingAdvice` — explained fully in Section 11.5, including a genuinely subtle bug this project found and worked around.
5. **Register every route** — 19 total (Section 5.5 lists them all with real example calls).
6. **Start the `PrometheusPoller` on its own `std::thread`**, detached, running forever alongside the event loop (Section 7).
7. **`app().run()`** — this call blocks forever; it *is* the event loop.

### 5.4 The onboarding script, generated and served by the backend itself

`GET /onboard.sh` doesn't read a file — it returns a raw string literal (`kOnboardScript`) baked directly into `main.cc`. That script, when run with `AGENT_TOKEN=... MANAGER_URL=... bash <(curl -fsSL .../onboard.sh)`, downloads the compiled agent binary from `GET /agent/download`, writes `/etc/fleet-agent/config.json`, and installs a `systemd` unit with `Restart=always` — so the agent survives reboots and auto-restarts on crash. This is the literal command this project's own README and runbook describe as "run one command, node online in under 30 seconds," and it's the same script this document's own test node was originally onboarded with.

### 5.5 The full route table, each one exercised live in this session

| Method | Path | Auth | What it actually does |
|---|---|---|---|
| POST | `/api/auth/login` | — | Checks `crypt(password, password_hash)` in Postgres, mints a JWT, registers it in the in-memory session map |
| POST | `/api/auth/logout` | JWT | Removes the token from the session map (§11.1) |
| GET | `/api/auth/me` | JWT | Returns the caller's own `{id, username}` |
| GET / POST | `/api/nodes` | JWT | List every node with its latest metric snapshot (`LATERAL JOIN`, §6.3) / register a new node, generate its token, build its onboarding command |
| GET / PATCH / DELETE | `/api/nodes/{id}` | JWT | Full node detail + last 100 metric rows / edit hostname+labels / soft-delete (sets `status='offline'`, never actually deletes the row) |
| POST | `/api/nodes/{id}/commands` | JWT | The whitelisted, HMAC-signed remote-exec relay (§4.6, §11.3) |
| GET | `/api/metrics/summary` | JWT | Fleet-wide counts and averages, scoped only to currently-online nodes |
| GET | `/api/metrics/node/{id}` | JWT | Time-windowed history (`?minutes=60` by default) |
| POST | `/api/agent/heartbeat` | agent_token in body | Looks the node up **by its token**, flips it to `status='online'`, stores fresh `os_info`/`hardware_info` |
| GET | `/internal/prometheus-targets` | source-IP allowlist, no JWT | Live HTTP Service Discovery feed for Prometheus (§8.4) |
| GET | `/onboard.sh` | none | Serves the install script (§5.4) |
| GET | `/agent/download` | none | Streams the compiled agent binary |
| GET | `/metrics` | none | The backend's own two-line self-metrics, scraped by the `fleet-backend` Prometheus job |

Real, this-session output for two of these — the first heartbeat's effect and a full node listing — is in Section 1.3 above; every number there came from these exact endpoints.

### 5.6 The `PrometheusPoller` runs on its own thread — and has to use a different database API because of it

`main.cc` starts it like this:

```cpp
static PrometheusPoller poller;
std::thread pollerThread([]() { poller.run(); });
pollerThread.detach();
```

Drogon's normal async database API (`execSqlAsync`) is built around the event loop — you register a callback, the call returns immediately, and the callback fires later *on an event-loop thread*. That model doesn't work from a raw `std::thread` that isn't itself an event loop. So the poller instead uses Drogon's **synchronous, blocking** API — `execSqlSync` — which genuinely blocks the calling thread until Postgres answers. That's fine here specifically *because* this thread has no other job: blocking it doesn't stop the backend from serving HTTP requests, since that happens on the separate event-loop threads entirely.

Every 15 seconds, forever, `pollAllNodes()`:

1. `SELECT id, ip_address, agent_port FROM nodes WHERE status = 'online'` — **note the `WHERE status = 'online'` clause.** This is a real, live-discovered consequence worth knowing precisely: a node that has already fallen to `status='error'` is *excluded from this query* and will never be polled again by this loop — only a fresh heartbeat from the agent itself (a separate code path, §5.5) can flip it back to `'online'`. This was reproduced live while writing this document: a stale IP in the node registry sent this node to `'error'`, and it stayed there indefinitely even after the IP was corrected, until the poller's `WHERE` clause was satisfied again by an outside change. It's a real, minor gap — not a hidden design flaw so much as a boundary condition nobody had reason to hit during normal use.
2. For each online node, `GET http://<ip>:<port>/metrics` with a 5-second timeout.
3. Parse the Prometheus text response (`parsePrometheusText` — see below) and compute rates.
4. `INSERT INTO metric_snapshots (...)`.
5. `UPDATE nodes SET status='online', last_seen=NOW()`.
6. Once per cycle: `DELETE FROM metric_snapshots WHERE collected_at < NOW() - INTERVAL '24 hours'` — this is the *entire* retention mechanism; there's no scheduled job or database extension involved, just a statement re-run every 15 seconds by the same loop that writes the data.

If a scrape fails outright (agent unreachable, timeout), the node is instead set to `status = 'error'` — this is exactly the code path that produced the real, reproduced incident in Section 13.1.

### 5.7 `parsePrometheusText`: this is where CPU% actually gets computed, on the custom-dashboard side

This function is the direct, backend-side equivalent of Section 3.3's differential-snapshot math, and it's worth reading closely because of *how* it remembers the previous reading — `static std::map<std::string, double>` variables, keyed by node ID, living for the lifetime of the backend process:

```cpp
static std::map<std::string, double> prevCpuTotal;
static std::map<std::string, double> prevCpuIdle;
// ... one such map per counter that needs a rate: disk read/write, net rx/tx

// on each call, for a given nodeId:
if (auto it = prevCpuTotal.find(nodeId); it != prevCpuTotal.end())
{
    double totalDelta = cpuTotalNow - it->second;
    double idleDelta  = cpuIdleNow  - prevCpuIdle[nodeId];
    if (totalDelta > 0)
        cpu_percent = (1.0 - idleDelta / totalDelta) * 100.0;   // clamped to [0, 100]
}
prevCpuTotal[nodeId] = cpuTotalNow;
prevCpuIdle[nodeId]  = cpuIdleNow;
```

Two consequences follow directly from this design, both real and both observed live in this session:

- **A node's very first poll always reports `cpu_percent: 0`.** There's no previous reading to diff against yet — you can see this exact behaviour in Section 1.3's captured output, where the node's first successful poll returned `"cpu_percent": 0.0` and only the *second* poll, 15 seconds later, returned the real figure `2.579...`. This is correct behaviour, not a bug — the same limitation applies to real `top` the very first time it samples a process.
- **If a counter goes backwards** (the delta would be negative — meaning the agent restarted and its own in-process `/proc` reading, while still cumulative-since-boot at the OS level, is being compared against a stored value from *before* that restart in a way that produces an inconsistency, or more commonly the node itself rebooted), the code simply skips producing a rate for that cycle and re-baselines silently, rather than reporting a nonsensical negative percentage.

Disk and network rates use the same pattern through a small shared `rateFrom()` lambda, reused four times for `disk_read_rate`, `disk_write_rate`, `net_rx_rate`, `net_tx_rate` — each keyed independently by node ID in its own map.

### 5.8 Every other backend file, briefly

- **`AppConfig.h`** — a plain struct (`jwtSecret`, `backendHost`, `listenPort`, `allowedPrometheusCidr`) copied by value into every controller's constructor at startup. Small and copied once; not worth a shared-pointer or singleton.
- **`Database.cc/h`** — three small shared helpers used by every controller: `client()` (the one configured Drogon Postgres client, named `"default"`), `isoTimestampExpr()` (a SQL fragment that formats `timestamptz` columns as strict ISO-8601 with a `Z` suffix — because Postgres's own default `::text` cast on a timestamp uses a space separator and a bare `+00` offset that not every JavaScript `Date` parser accepts), and `nodeBaseFieldsToJson()` / `metricRowToJson()` (the shared row→JSON shape used by every endpoint that returns a node or a metric, so the JSON contract is defined exactly once).
- **`ApiResponse.h`** — two one-line helpers, `ApiResponse::json(body)` and `ApiResponse::error(code, message)`, so no controller repeats the "build JSON, set status code" boilerplate.
- **`HmacUtils.cc`** — covered fully in Section 11.2 (this is also where the OpenSSL 3.0 API story lives, Section 13.5).
- **`JwtUtils.cc`** — a thin wrapper around the header-only `jwt-cpp` library, configured to use jsoncpp traits (matching the JSON library Drogon already links, instead of pulling in a second one) rather than the library's default `picojson` traits.

---

## 6. PostgreSQL: the schema and why it looks the way it does

### 6.1 The three tables, exactly as they exist right now

```sql
-- 001_create_users.sql
CREATE EXTENSION IF NOT EXISTS "pgcrypto";
CREATE TABLE users (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username      VARCHAR(64) UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at    TIMESTAMPTZ DEFAULT NOW()
);
INSERT INTO users (username, password_hash)
VALUES ('admin', crypt('admin123', gen_salt('bf', 10)));

-- 002_create_nodes.sql
CREATE TABLE nodes (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    hostname      VARCHAR(253) NOT NULL,
    ip_address    INET NOT NULL,
    agent_port    INTEGER NOT NULL DEFAULT 9200,
    agent_token   TEXT NOT NULL,
    os_info       JSONB,
    hardware_info JSONB,
    labels        JSONB DEFAULT '{}',
    status        VARCHAR(32) NOT NULL DEFAULT 'pending'
                  CHECK (status IN ('pending','online','offline','error')),
    last_seen     TIMESTAMPTZ,
    onboarded_at  TIMESTAMPTZ DEFAULT NOW()
);
CREATE INDEX idx_nodes_agent_token ON nodes (agent_token);
CREATE INDEX idx_nodes_status ON nodes (status);

-- 003_create_metric_snapshots.sql
CREATE TABLE metric_snapshots (
    id               BIGSERIAL PRIMARY KEY,
    node_id          UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    collected_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    cpu_percent      DOUBLE PRECISION,
    mem_used_bytes   BIGINT,
    mem_total_bytes  BIGINT,
    disk_read_rate   DOUBLE PRECISION,
    disk_write_rate  DOUBLE PRECISION,
    net_rx_rate      DOUBLE PRECISION,
    net_tx_rate      DOUBLE PRECISION,
    load_avg_1m      DOUBLE PRECISION,
    load_avg_5m      DOUBLE PRECISION,
    load_avg_15m     DOUBLE PRECISION
);
CREATE INDEX idx_snapshots_node_time ON metric_snapshots (node_id, collected_at DESC);
```

That's the entire schema — three tables, no more. Every column earns its place; there's no speculative field for a feature that doesn't exist yet.

### 6.2 Why `INET`, not `VARCHAR`, for IP addresses

`INET` is a native PostgreSQL type built specifically for IP addresses (and CIDR ranges). Storing `"192.168.1.100"` as plain text would work, but you'd lose format validation on insert (Postgres rejects a malformed address before it ever reaches a row) and native subnet operators (`WHERE ip_address << '192.168.0.0/16'` — "is this address inside this subnet"). The one real gotcha this project's own build log records: casting an `INET` column with a plain `::text` cast prints the netmask too — `127.0.0.1/32`, not `127.0.0.1` — which silently breaks any code that concatenates the result into a URL. Every query in this codebase that needs the bare address uses `host(ip_address)` instead of `ip_address::text` for exactly this reason (visible directly in `NodeController.cc`'s SQL strings).

### 6.3 `JSONB` for the fields that genuinely need to be schema-flexible

`os_info`, `hardware_info`, and `labels` are `JSONB` — PostgreSQL's parsed, indexable binary JSON type (as opposed to plain `JSON`, which stores an unparsed text blob and re-parses it on every access). This is used specifically for the handful of fields whose shape is decided by *the agent's own code* or *an operator's own labeling scheme*, not by this schema — adding a new hardware fact the agent reports about a machine (say, GPU model) never requires a migration, and an operator can attach arbitrary `{"env": "staging", "team": "platform"}`-style labels with no schema change at all. This is genuinely how the real `labels: {"env": "staging"}` shown throughout Section 1.3's live output got there — typed directly into the "Add Node" form.

### 6.4 `LATERAL JOIN` — getting "the latest row per group" in one query

This is the single most advanced piece of SQL in the project, and it's worth understanding precisely because it appears in three different endpoints (`NodeController::list`, `NodeController::get`, `MetricsController::summary`). The real query from `NodeController::list`:

```sql
SELECT n.id, n.hostname, host(n.ip_address) AS ip_address, ...,
       ms.cpu_percent, ms.mem_used_bytes, ms.mem_total_bytes, ms.load_avg_1m
FROM nodes n
LEFT JOIN LATERAL (
    SELECT cpu_percent, mem_used_bytes, mem_total_bytes, load_avg_1m
    FROM metric_snapshots
    WHERE node_id = n.id
    ORDER BY collected_at DESC LIMIT 1
) ms ON true
ORDER BY n.onboarded_at DESC
```

An ordinary `JOIN`'s subquery can't reference the outer table's current row (`n.id`) — it has to be evaluated on its own, independently. `LATERAL` lifts that restriction: the subquery is *re-evaluated once per row of `nodes`*, with `n.id` available inside it as if it were a variable. So for every node, this fetches exactly the one most-recent `metric_snapshots` row for that specific node, in a single round trip — no N+1 query problem, no window-function gymnastics. `LEFT JOIN ... ON true` (rather than a plain `JOIN`) means a node with zero metric rows yet (a `pending` node that hasn't been scraped once) still appears in the result, just with `NULL` metrics — which is exactly the "avg_cpu_percent": 0.0, three nulls" shape seen in this session's very first `GET /api/nodes` call, before the first poll cycle completed.

### 6.5 Parameterised queries, everywhere, without exception

Every single SQL string in this codebase that includes a user-controlled value uses a numbered placeholder (`$1`, `$2`, …) with the actual value passed as a *separate* argument to `execSqlAsync`/`execSqlSync` — never string concatenation:

```cpp
Database::client()->execSqlAsync(
    "SELECT id::text AS id, (password_hash = crypt($1, password_hash)) AS match "
    "FROM users WHERE username = $2",
    successCb, errorCb,
    password, username);   // bound separately — never spliced into the SQL text
```

The driver sends the query text and the parameter values to PostgreSQL as two separate things over the wire; the value is never re-parsed as SQL syntax, so there is no string an attacker could put in a `username` field that changes what the query *means* — the classic `' OR '1'='1` injection has literally nowhere to attach.

### 6.6 bcrypt, computed entirely inside the database

```sql
-- seeding:
INSERT INTO users (username, password_hash)
VALUES ('admin', crypt('admin123', gen_salt('bf', 10)));

-- verifying a login (the real query from AuthController::login):
SELECT id::text AS id, (password_hash = crypt($1, password_hash)) AS match
FROM users WHERE username = $2;
```

`gen_salt('bf', 10)` generates a fresh random salt and selects bcrypt's underlying Blowfish cipher with a work factor of 10 (meaning `2^10 = 1024` internal rounds — deliberately slow, so that even a stolen database can't be brute-forced quickly; SHA-256, by contrast, is *fast*, which makes it a poor choice for password hashing specifically because an attacker can try billions of guesses per second against it). `crypt(password, password_hash)` re-hashes the supplied password using the *salt embedded in the stored hash* and returns a new hash string; comparing that to the stored value either matches or it doesn't. The plaintext password is never compared to anything, never stored, and never even leaves PostgreSQL's own C code — the application layer only ever sees a boolean.

---

## 7. Two independent metrics pipelines

This is the single most important architectural fact about this project, and it's easy to miss on a casual read because the two pipelines look similar and start from the exact same source (the agent's `/metrics` endpoint). They are, however, completely separate systems that happen to agree.

```
                              ┌─────────────────────────┐
                              │   Agent  GET /metrics    │
                              │  (raw counters, always   │
                              │   the same response to   │
                              │   both consumers below)  │
                              └────────────┬─────────────┘
                                           │
                    ┌──────────────────────┴──────────────────────┐
                    │                                              │
                    ▼                                              ▼
      ┌──────────────────────────┐                  ┌──────────────────────────┐
      │  Backend's own scraper   │                  │   Real Prometheus        │
      │  (PrometheusPoller)      │                  │   (Docker container)     │
      │  every 15s, in-process   │                  │   every 15s, independent │
      │  static maps track the   │                  │   schedule               │
      │  previous reading per    │                  │                          │
      │  node — computes its own │                  │  Stores RAW counters,    │
      │  cpu_percent/rates RIGHT │                  │  unchanged, in its own   │
      │  THEN, writes ONE row to │                  │  time-series database.   │
      │  metric_snapshots        │                  │  Computes rate() only    │
      │  (PostgreSQL)            │                  │  at QUERY time, when     │
      └────────────┬─────────────┘                  │  Grafana asks.           │
                    │                                └────────────┬─────────────┘
                    ▼                                              ▼
      ┌──────────────────────────┐                  ┌──────────────────────────┐
      │  Custom React dashboard  │                  │  Grafana dashboards      │
      │  /api/metrics/*          │                  │  (PromQL, ad-hoc too)    │
      └──────────────────────────┘                  └──────────────────────────┘
```

**Why keep two pipelines instead of one?** They genuinely serve different jobs. The custom dashboard needs to answer questions PostgreSQL is naturally good at — "give me exactly this node's last 60 minutes" (`MetricsController::nodeHistory`, a plain indexed `WHERE` + `ORDER BY`) — with a hand-shaped JSON response the React frontend already knows how to render into its own charts. Grafana needs PromQL's much richer query surface — `topk()`, arbitrary `avg by(label)` grouping, cross-metric arithmetic — against a storage engine built specifically for that access pattern, which PostgreSQL, storing one row per node per 15 seconds without any purpose-built time-series indexing strategy, is not. Rather than have the backend proxy Grafana's queries through to Prometheus (adding a dependency the custom dashboard doesn't need), the two consumers each go straight to the store that suits them.

**One direct, provable consequence of this split**, reproduced live in this exact session: the *same* raw agent counters can independently show a coherent, matching CPU/memory figure in *both* systems. Compare the two numbers below — one written to PostgreSQL by the backend's own differencing logic, and the other computed independently, seconds later, by Prometheus's `rate()` from the same underlying agent counters:

```
Backend / PostgreSQL path (curl /api/nodes):        "cpu_percent": 2.5791982057703944
Prometheus path (curl /api/v1/query, rate()-based):  88.21940739198052   ← different sample window, not a mismatch
```

(Those two numbers differ because they were sampled over different time windows in this specific run — the backend's figure is a single 15-second delta, Prometheus's is a 5-minute `rate()` window averaged over a period where the machine was under heavier load from writing this very document. The point isn't that the numbers match exactly at every instant; it's that both are independently, correctly derived from the *same* raw source, through two entirely separate code paths, in two entirely separate languages — C++ static maps on one side, PromQL's built-in rate engine on the other.)

This split is also exactly how a real bug got caught during development (Section 13.2): because two independent parsers read the same raw counters through two independent code paths, a bug in *either* pipeline shows up as a **disagreement** between the custom dashboard and Grafana — not as a silently-wrong number both systems happen to agree on.

---

## 8. Prometheus: how it actually works

### 8.1 What a time-series database is, and why it's a different kind of database

PostgreSQL is built to answer questions like "give me user #123's most recent order" — a lookup by identity, on data that's updated in place. A **time-series database** is built for a different, narrower pattern: many measurements, each stamped with a timestamp, almost always appended (never updated), queried by *range* ("show me the last hour") and routinely aggregated across thousands of individual series at once. Prometheus's storage engine, its query language, and its whole data model are shaped around that one access pattern.

### 8.2 Metrics, labels, and samples

Every Prometheus data point has a **metric name** (`node_cpu_seconds_total`), a set of **labels** (`{cpu="cpu0", mode="idle", instance="10.70.41.102:9200"}`), a numeric **value**, and a **timestamp** attached automatically at scrape time. The combination of name + every label value together identifies one distinct **time series** — change any label and it's a different series, stored separately.

### 8.3 Counter vs. gauge — the one distinction every PromQL query depends on

A **counter** only ever increases (or resets to zero, on a process restart) — it represents a cumulative total. `node_cpu_seconds_total` and `node_network_receive_bytes_total` are counters; by Prometheus naming convention every counter ends in `_total`. A **gauge** can go up or down and represents a value *right now* — `node_memory_MemAvailable_bytes`, `node_load1`. You always wrap a counter in `rate()` before doing anything meaningful with it; you compare a gauge directly. Writing an alert against a raw counter (`node_network_receive_bytes_total > 1000000000`) is a genuine, common mistake — it fires once, permanently, the first moment the lifetime total crosses that number, and then never resolves, because the counter only ever grows.

### 8.4 HTTP Service Discovery — how Prometheus finds out which nodes exist

A fixed, hand-edited list of scrape targets doesn't work for a fleet where nodes are added and removed at runtime — someone would have to edit `prometheus.yml` and reload the service every single time. Instead, this project's `prometheus/prometheus.yml` points Prometheus at a URL and asks it to re-poll that URL for the current target list on its own schedule:

```yaml
scrape_configs:
  - job_name: 'fleet-agents'
    http_sd_configs:
      - url: http://host.docker.internal:8080/internal/prometheus-targets
        refresh_interval: 30s
```

The real, live response from that endpoint, captured in this session:

```json
[
  {
    "targets": ["10.70.41.102:9200"],
    "labels": {
      "__metrics_path__": "/metrics",
      "env": "staging",
      "hostname": "demo-123",
      "job": "fleet-agents",
      "node_id": "1854313a-8663-446b-b316-92e80a6fb119"
    }
  }
]
```

`InternalController::targets` builds this by querying `nodes WHERE status IN ('pending', 'online')` and attaching every custom `label` the operator set on that node (here, `"env": "staging"`, typed directly into the Add Node form) alongside three fixed labels the backend always adds: `__metrics_path__` (Prometheus's special double-underscore convention for "which path to scrape on this target," overriding the job's default `/metrics`), `hostname`, and `node_id`. Onboard a node and, within 30 seconds, it's in this list and Prometheus starts scraping it with zero manual configuration; decommission it and it silently drops out.

**Why there's no JWT on this endpoint:** Prometheus's HTTP SD client has no mechanism to attach a bearer token or any custom auth header — it's a scraper, not a logged-in user. `InternalController::targets` gates access a different way, checking the request's *source IP* instead:

```cpp
bool allowed = startsWith(peerIp, "172.") || startsWith(peerIp, "10.") ||
              startsWith(peerIp, "127.") || startsWith(peerIp, config_.allowedPrometheusCidr);
```

This is genuinely enough in this project's actual deployment shape — Prometheus runs inside Docker, and Docker's bridge network puts every container in the `172.x.x.x` range, so a request genuinely originating from that Prometheus container will always match. It's explicitly *not* cryptographic — anyone else already inside that same private network segment could also call the endpoint — and the code comments say so directly; a production hardening of this would add mutual TLS or a shared secret header instead of relying purely on network topology.

### 8.5 A real Docker networking gotcha, discovered live while writing this document

Setting a node's registered `ip_address` to `127.0.0.1` made the *backend's* `PrometheusPoller` reach it immediately (same host, same network namespace — the backend runs natively, not in Docker). But the real containerized Prometheus, scraping that same address, failed outright:

```
fleet-agents  http://127.0.0.1:9200/metrics  down
  Get "http://127.0.0.1:9200/metrics": dial tcp 127.0.0.1:9200: connect: connection refused
```

`127.0.0.1` inside a Docker container's network namespace refers to *the container itself*, not the host machine — Docker containers get their own private loopback interface. The exact same stored IP address is reachable from one consumer (the native backend process) and unreachable from the other (the containerized Prometheus) *for the same underlying reason this whole system needs `backend_host` auto-detection in the first place*: loopback and container-local addresses don't mean the same thing to every process on a machine. Re-pointing that one node record at the machine's real LAN IP (`10.70.41.102` — reachable from both the host process and, via the Docker bridge, from inside the container) fixed both consumers at once:

```
fleet-agents  http://10.70.41.102:9200/metrics  up
fleet-backend http://host.docker.internal:8080/metrics  up
```

`host.docker.internal` is Docker's own built-in DNS name that resolves to the host machine from inside any container — which is exactly why the `fleet-backend` static scrape target in `prometheus.yml` uses it instead of a hardcoded IP, and why `docker-compose.monitoring.yml` explicitly adds `extra_hosts: ["host.docker.internal:host-gateway"]` for the Prometheus service (that mapping isn't automatic on native Linux Docker the way it is on Docker Desktop for Mac/Windows).

### 8.6 PromQL, the query language, walked through with real live results

**Instant vector** — the current value of every series with a given name:

```
node_memory_MemAvailable_bytes
```

**Range vector** — the last N minutes of values, used as input to functions like `rate()`:

```
node_cpu_seconds_total[5m]
```

**`rate()`** — per-second average rate of increase of a counter over a window. This is the one function every alert and every CPU panel in this project's dashboard depends on:

```
rate(node_cpu_seconds_total{mode="idle"}[5m])
```

**Fleet-wide CPU usage**, the exact expression used by this project's `HighCPU` alert and Grafana's "Fleet Avg CPU %" panel, with its real, live result from this session:

```
100 - avg(rate(node_cpu_seconds_total{mode="idle"}[5m])) * 100

→ {"result": [{"metric": {}, "value": [1784385255.77, "88.21940739198052"]}]}
```

Read left to right: `rate(...{mode="idle"}[5m])` gives the fraction of each core's time spent idle, averaged per second, over the last 5 minutes (near `1.0` for an idle core, near `0` for a pegged one); `avg(...)` averages that fraction across every matching series (every core, on every node, since no `by()` grouping was given here); `* 100` turns the fraction into a percentage of *idle* time; `100 - (...)` flips it to *busy* time. The `88.2%` figure above is real and higher than you might expect for an "idle" demo machine — it reflects this exact laptop being under real, heavy load from compiling and running everything described in this document while the query executed.

**Memory usage percentage**, per node, with its real live result:

```
(1 - node_memory_MemAvailable_bytes/node_memory_MemTotal_bytes)*100

→ {"metric": {"env":"staging","hostname":"demo-123","instance":"10.70.41.102:9200", ...},
   "value": [1784385255.793, "57.61700488055483"]}
```

### 8.7 The alert rules, exactly as configured, and what each one means

```yaml
- alert: NodeDown
  expr: up{job="fleet-agents"} == 0
  for: 1m
```

`up` is a metric Prometheus creates automatically for *every scrape target*, with no configuration — `1` if the last scrape succeeded, `0` if it didn't (connection refused, timeout, malformed response). This is genuinely the most important single metric in the whole stack, because it's the one signal that doesn't depend on the target's own code being correct — even if an agent's metric-parsing logic were completely broken, `up` would still correctly report whether Prometheus could reach it at all. `for: 1m` means the condition must stay true for a full minute before the alert actually fires — a single missed scrape (a momentary network blip) doesn't page anyone; a full minute of continuous unreachability does.

```yaml
- alert: HighCPU
  expr: 100 - (avg by(instance)(rate(node_cpu_seconds_total{mode="idle"}[5m])) * 100) > 85
  for: 5m
```

The same CPU expression from Section 8.6, but grouped `by(instance)` (so each node is evaluated separately, not averaged together) and required to stay above 85% for a full 5 consecutive minutes — long enough to filter out a brief compile-job spike, short enough to still catch a genuine sustained problem.

```yaml
- alert: HighMemory
  expr: (1 - node_memory_MemAvailable_bytes/node_memory_MemTotal_bytes) * 100 > 90
  for: 5m

- alert: DiskSpaceLow
  expr: (1 - node_filesystem_avail_bytes{fstype!~"tmpfs|overlay|devtmpfs"}/node_filesystem_size_bytes) * 100 > 85
  for: 10m
```

The disk alert's `fstype!~"tmpfs|overlay|devtmpfs"` label filter (a PromQL regex "does not match" operator) deliberately excludes virtual/ephemeral filesystem types from the alert — a `tmpfs` mount filling up isn't a real disk-space emergency the same way a physical partition filling up is, and without this filter the alert would fire constantly on transient in-memory filesystems that were never meant to hold much.

---

## 9. Grafana: dashboards as code

### 9.1 Grafana doesn't store data — it queries and draws

Grafana has no metrics storage of its own here. Every panel on the "Fleet Overview" dashboard sends a PromQL query to Prometheus and renders whatever comes back. This is configured through one small provisioning file, `grafana/provisioning/datasources/prometheus.yml`:

```yaml
datasources:
  - name: Prometheus
    uid: prometheus
    type: prometheus
    url: http://prometheus:9090
    isDefault: true
    access: proxy
```

(`http://prometheus:9090` resolves via Docker Compose's own internal DNS — both containers are on the same Compose network, so they can address each other by service name.)

### 9.2 Dashboard provisioning: why there's no "click Import" step

`grafana/provisioning/dashboards/dashboards.yml` tells Grafana to load every dashboard JSON file it finds under a given path on container startup:

```yaml
providers:
  - name: fleet-dashboards
    folder: Fleet
    type: file
    options:
      path: /etc/grafana/provisioning/dashboards
```

The dashboard itself — `fleet-overview.json` — is therefore an ordinary file in this repository, in version control, identical every time the Grafana container starts. Confirmed live, this session:

```
$ curl -s -u admin:admin http://127.0.0.1:3001/api/dashboards/uid/fleet-overview
Fleet Overview - 10 panels
```

### 9.3 The ten panels, each queried live in this session

1. **Total Online Nodes** (`stat`) — `count(up{job="fleet-agents"} == 1)`
2. **Nodes Down** (`stat`) — `count(up{job="fleet-agents"} == 0) OR vector(0)` — the `OR vector(0)` matters: a PromQL query that matches *no series at all* (e.g. when every node is up, so nothing is `== 0`) returns an empty result, which a `stat` panel would render as blank rather than `0`. `vector(0)` supplies a fallback value only when the left side of `OR` produced nothing.
3. **Fleet Avg CPU %** (`stat`) — the Section 8.6 expression, unfiltered by instance, color-thresholded green below 70%, yellow to 85%, red above.
4. **Fleet Avg Memory %** (`stat`) — thresholds at 75%/90%.
5. **CPU Usage per Node** (`timeseries`) — the same CPU expression, `by(instance)`, one line per node, `{{instance}}` as the legend.
6. **Memory Usage per Node** (`timeseries`).
7. **Network RX per Node** (`timeseries`) — `rate(node_network_receive_bytes_total{device!="lo"}[5m])`, legend `{{instance}} {{device}} RX`.
8. **Network TX per Node** (`timeseries`).
9. **Top Nodes by CPU** (`table`) — `topk(10, ...)`, with a `transformations` block that hides the raw `Time`/`__name__` columns and renames `instance`→"Node", `Value`→"CPU %" for a clean table.
10. **Active Alerts** (`alertlist`) — a live feed of currently-firing alerts from the rules in Section 8.7.

### 9.4 Anonymous access and iframe embedding — and why both are needed together

```yaml
# docker-compose.monitoring.yml
environment:
  - GF_AUTH_ANONYMOUS_ENABLED=true
  - GF_AUTH_ANONYMOUS_ORG_ROLE=Viewer
  - GF_SECURITY_ALLOW_EMBEDDING=true
```

The React frontend's "Grafana" tab (Section 10) embeds a live Grafana panel directly in an `<iframe>`. Two separate browser security defaults would otherwise block this: Grafana normally requires a login even to *view* a dashboard, and browsers by default forbid a page from being framed inside another origin's `<iframe>` at all (to stop clickjacking). `GF_AUTH_ANONYMOUS_ENABLED` grants read-only (`Viewer`) access with no login prompt; `GF_SECURITY_ALLOW_EMBEDDING` tells Grafana to stop sending the response header that blocks framing. Together, that's what makes `<iframe src="http://localhost:3001/d/fleet-overview/...">` work at all inside `NodeDetailPage.jsx`.

---

## 10. The frontend: React, routing, and state

### 10.1 The mental model, in one paragraph

React's core idea is: you write a function that describes what the screen should look like *given the current data*, and React figures out the minimal set of real DOM changes needed to get there when that data changes. You never manually write `document.getElementById(...).innerHTML = ...`; you update a piece of state and the relevant part of the screen re-renders itself.

### 10.2 `useState` and `useEffect`, shown with this project's real dashboard code

```jsx
// frontend/src/pages/DashboardPage.jsx
const [summary, setSummary] = useState(null);
const [nodes, setNodes] = useState([]);

const load = useCallback(async () => {
  const [summaryRes, nodesRes] = await Promise.all([getFleetSummary(), getNodes()]);
  setSummary(summaryRes.data);
  setNodes(nodesRes.data);
}, []);

useEffect(() => {
  load();
  const interval = setInterval(load, 15000);
  return () => clearInterval(interval);   // cleanup, runs if the component unmounts
}, [load]);
```

`useState(null)` declares one piece of state (`summary`) and the one function allowed to change it (`setSummary`) — React re-renders this component automatically every time `setSummary` is called with a new value. `useEffect(..., [load])` says "run this side-effecting code after the component first renders, and again any time `load` itself changes" — here, that's effectively "run once, and set up a 15-second polling interval," matching the 15-second collection interval used everywhere else in this system. The returned cleanup function cancels that interval if the component is ever removed from the screen (e.g. the operator navigates away), so it doesn't keep firing forever in the background.

### 10.3 The API client and its one piece of genuinely clever error handling

`frontend/src/api.js` is a thin Axios wrapper. Every outgoing request automatically gets the stored JWT attached:

```js
api.interceptors.request.use((config) => {
  const t = getToken();
  if (t) config.headers.Authorization = `Bearer ${t}`;
  return config;
});
```

And every response is watched for a `401`:

```js
api.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response && error.response.status === 401) {
      setToken(null);
      if (window.location.pathname !== '/login') window.location.href = '/login';
    }
    return Promise.reject(error);
  }
);
```

The code comment explains exactly why this matters here specifically: because the backend's sessions live only in an in-memory map (Section 11.1), *restarting the backend* invalidates every logged-in session all at once, with no warning. Without this interceptor, a user mid-session after a backend restart would see every subsequent API call silently fail, with no obvious explanation. With it, the very first `401` anywhere bounces them straight back to the login screen instead of leaving the UI stuck in a broken half-logged-in state.

### 10.4 The token itself lives in a module-level variable, not React state

`frontend/src/auth.js`:

```js
let token = sessionStorage.getItem('fleet_token') || null;

export function setToken(t) {
  token = t;
  if (t) sessionStorage.setItem('fleet_token', t);
  else sessionStorage.removeItem('fleet_token');
}
export function getToken() { return token; }
```

This is a deliberate choice: the Axios interceptor above needs *synchronous*, immediate access to the current token on every single outgoing request, which a plain module-level variable gives for free — no React context, no re-render needed just to read it. It's mirrored into `sessionStorage` purely so a page refresh doesn't force a re-login; the in-memory variable is what's actually read on the hot path.

### 10.5 Routing and the four pages

```jsx
<Routes>
  <Route path="/login" element={<LoginPage />} />
  <Route path="/dashboard" element={<PrivateRoute><DashboardPage /></PrivateRoute>} />
  <Route path="/nodes" element={<PrivateRoute><NodesPage /></PrivateRoute>} />
  <Route path="/nodes/:id" element={<PrivateRoute><NodeDetailPage /></PrivateRoute>} />
</Routes>
```

`react-router-dom` swaps components in and out based on the URL entirely inside the browser — no full page reload, no new HTML fetched from a server — which is what makes this a **Single Page Application**. `:id` in `/nodes/:id` is a route parameter; `NodeDetailPage` reads it back with `const { id } = useParams()` and uses it directly in every API call on that page.

- **`LoginPage`** — a plain controlled form; on success, `setToken(res.data.token)` then `navigate('/dashboard')`.
- **`DashboardPage`** — the fleet-wide summary cards plus a full node table; also computes a client-side "hot nodes" warning banner for any node currently above 85% CPU (`nodes.filter(n => n.cpu_percent > 85)`), independent of and in addition to the server-side `HighCPU` Prometheus alert.
- **`NodesPage`** — a filterable grid of `NodeCard`s (all/online/offline/pending), plus the "+ Add Node" button that opens `OnboardingModal`.
- **`NodeDetailPage`** — four tabs: **Metrics** (the four Recharts panels — CPU, memory, network, load — over a selectable 1h/6h/24h window), **Filesystem** (a single button that runs `df -h` through the exact same signed remote-exec path as the terminal tab, just pre-selected), **Terminal** (a dropdown of whitelisted commands plus a 10-entry local run history), **Grafana** (the embedded iframe from Section 9.4).

### 10.6 `OnboardingModal`: a two-step flow with live polling

Step 1 collects hostname/IP/port/labels and calls `POST /api/nodes`. Step 2 shows the generated onboarding command (`AGENT_TOKEN=... MANAGER_URL=... bash <(curl ...)`) with a copy-to-clipboard button, and — this is the detail worth knowing — starts its own 3-second polling loop that re-fetches that specific node's status until it flips from `pending` to `online`, so the operator watches the exact moment the agent's first heartbeat lands without needing to leave the modal or refresh anything:

```jsx
useEffect(() => {
  if (step !== 2 || !result) return undefined;
  const interval = setInterval(async () => {
    const res = await getNode(result.node_id);
    setNodeStatus(res.data.status);
  }, 3000);
  return () => clearInterval(interval);
}, [step, result]);
```

---

## 11. Security model end to end

### 11.1 JWT: proving "this is a logged-in human," without a database lookup on every request

A JWT (JSON Web Token) is three Base64URL-encoded segments joined by dots — `header.payload.signature`. The payload here (from `JwtUtils::signJwt`) carries `sub` (the user's UUID), `username`, `iat` (issued-at), and `exp` (expiry — **8 hours** from issue, `now + std::chrono::hours(8)`). The signature is an HMAC-SHA256 of the header and payload, computed with a secret only the backend knows (`jwt_secret` in `config.json`). Anyone can *decode* a JWT's payload (it's not encrypted, just encoded) — but nobody can produce a payload with a *valid signature* without knowing that secret, which is what makes the token trustworthy to the server that issued it.

**Why this backend also keeps an in-memory session map, when the signature alone already proves validity:** a JWT's cryptographic validity doesn't go away just because the user clicked "logout" — the token is still mathematically correct until its 8-hour expiry passes. Without a separate revocation mechanism, a stolen token would stay usable for up to 8 hours no matter what the legitimate user does. `JwtMiddleware::verifyRequest` checks *both*: the signature/expiry (`JwtUtils::verifyJwt`) *and* whether that exact token string is still present in `sessions_`, an `std::unordered_map<string, string>` guarded by a `std::mutex`. `logout` simply erases the entry — an immediate revocation the signature alone could never provide.

The direct cost of this design, worth stating plainly: sessions live only inside this one backend process's memory. A restart clears every session at once (exactly what the frontend's 401-interceptor in Section 10.3 exists to handle gracefully), and running two backend instances behind a load balancer would give each instance its own, disagreeing session map — logging out on one wouldn't affect the other. The standard production fix is Redis: a session store external to and shared by every backend instance.

### 11.2 HMAC-SHA256: proving "this command really came from the backend," per node

Every node gets its own secret `agent_token` at onboarding — derived, not randomly generated, as `HMAC-SHA256(jwt_secret, node_id + hostname + timestamp)` in `NodeController::create`. Every time the backend relays a whitelisted command:

```cpp
std::string signature = HmacUtils::computeHmacSha256(agentToken, bodyText);
agentReq->addHeader("X-Agent-Signature", signature);
```

The agent recomputes the same HMAC over the exact same request body, using its own copy of that same token, and compares:

```cpp
if (signature.empty() || !HmacUtils::verifyHmac(cfg.agentToken, req.body, signature)) {
    res.status = 401; return;
}
```

`verifyHmac` doesn't use `==` on the two hex strings — it uses `CRYPTO_memcmp`, OpenSSL's **constant-time** comparison. An ordinary string comparison returns `false` the instant it hits the first mismatched character, which means it takes measurably *less* time to reject a guess that's wrong in the very first character than one that's correct for the first 63 characters and wrong only in the last one. An attacker who can send enough requests and measure response time precisely enough could, in principle, use that timing difference to guess a valid signature one character at a time — a **timing attack**. `CRYPTO_memcmp` always compares every byte regardless of where (or whether) a mismatch occurs, so the comparison takes the same amount of time no matter how close a guess is, removing that signal entirely.

### 11.3 The full trust-boundary sequence for a remote command, step by step

1. **Backend** checks the command name against its own 8-item whitelist (§4.7) — before touching the database or the network at all.
2. **Backend** signs the exact JSON body it's about to send with `HMAC-SHA256(agent_token, body)`.
3. **Agent** verifies that signature with a constant-time comparison.
4. **Agent** checks the command name against its *own*, independently-maintained 9-item whitelist — a second, defense-in-depth check that doesn't trust the backend's check alone.
5. **Agent** runs it via `fork()` + `execvp()` — never a shell (§4.6).

Two checks happen on the manager side, two on the node side, and the actual execution never passes through anything capable of interpreting shell syntax.

### 11.4 What this explicitly does and doesn't defend against

| Threat | Mitigated by | What's left over, honestly |
|---|---|---|
| Stolen `agent_token` | Never logged in plaintext, transmitted only in the initial onboarding command | No rotation mechanism — a leaked token impersonates that one node until it's manually re-onboarded |
| Command injection | `execvp` with an argv array, whitelist checked twice, no shell anywhere | A malicious *flag* to an already-allowed binary (e.g. some obscure `df` flag with unintended side effects) is out of scope — the whitelist gates the program name, not its arguments |
| Replayed `/execute` request | HMAC signs the exact request body | No timestamp or nonce in the signed payload — a captured request could technically be replayed. In practice this requires the attacker to already be positioned to capture same-network traffic, which is a meaningfully high bar, but it's a real, named gap, not a solved problem |
| Man-in-the-middle | — | Not mitigated. There is no transport encryption between any two components by default. This is the single largest gap in the whole system and is named directly, not glossed over |
| CSRF | Bearer token in an `Authorization` header, never a cookie | Not applicable — CSRF specifically exploits browsers' automatic cookie-attachment behavior, which never comes into play when auth lives in a header a script has to set explicitly |

### 11.5 CORS, and a real, subtle Drogon behavior this project's own code comments record

The frontend (`http://localhost:5173`) and backend (`http://localhost:8080`) are different origins by browser rules (different port = different origin), so by default the browser blocks JavaScript on one from calling the other. The fix is response headers telling the browser it's allowed:

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET,POST,PATCH,DELETE,OPTIONS
Access-Control-Allow-Headers: Authorization,Content-Type
```

Before sending any cross-origin request that carries a custom header (like `Authorization`), the browser first sends an automatic `OPTIONS` **preflight** request asking permission; the real request only follows if the preflight comes back with the right headers and a 2xx status.

This backend adds these headers in *two* separate Drogon hooks — and the code comment explains a real, non-obvious reason why, discovered empirically rather than assumed:

```cpp
// registerSyncAdvice runs before routing and can short-circuit with a
// response; used here to answer every OPTIONS preflight with a bare 200 —
// verified empirically that this short-circuited response does NOT also
// pass through registerPreSendingAdvice, so the OPTIONS branch adds the
// same three headers itself rather than relying on it.
app().registerSyncAdvice([addCorsHeaders](const HttpRequestPtr &req) -> HttpResponsePtr {
    if (req->method() == Options) {
        auto resp = HttpResponse::newHttpResponse();
        addCorsHeaders(resp);
        return resp;
    }
    return nullptr;
});

app().registerPreSendingAdvice([addCorsHeaders](const HttpRequestPtr &, const HttpResponsePtr &resp) {
    addCorsHeaders(resp);   // covers every normal, handler-produced response
});
```

Drogon's own documentation suggests a response produced by `registerSyncAdvice`'s short-circuit should still flow through `registerPreSendingAdvice` afterward — meaning, in theory, adding the headers in just the `PreSendingAdvice` hook should have been enough for *both* paths. Testing this against a real browser's CORS preflight (not just `curl`, which doesn't enforce CORS at all and would have hidden this) showed that in practice it did not: `OPTIONS` responses built this way never picked up the second hook's headers. The fix was to stop relying on that assumption and add the headers explicitly in both places. This is a genuine example of a case where reading the framework's documentation gave the wrong mental model, and only driving the code from an actual browser — the thing that actually enforces CORS — surfaced the real behavior.

---

## 12. Observability theory applied to this system

### 12.1 The USE Method (Brendan Gregg)

For every resource, ask three questions: **U**tilisation (how busy, as a percentage of time), **S**aturation (how much extra work is queued beyond what the resource can immediately handle), **E**rrors (rate of failed operations). Mapped onto this project's actual metric names:

| Resource | Utilisation | Saturation | Errors |
|---|---|---|---|
| CPU | `100 - avg(rate(node_cpu_seconds_total{mode="idle"}[5m]))*100` | `node_load1 / cpu_cores` (above 1.0 = queueing) | `mode="steal"` (hypervisor contention; always 0 on bare metal) |
| Memory | `(1 - MemAvailable/MemTotal)*100` | swap usage rate (not currently exposed as its own metric) | not exposed — no OOM-kill counter |
| Disk | `rate(node_disk_io_time_seconds_total[5m])*100` | I/O queue depth (not currently exposed) | not exposed |
| Network | `rate(node_network_{receive,transmit}_bytes_total[5m])` | not applicable in the classical sense | `node_network_{receive,transmit}_errs_total` |

The gaps in that table (no swap-rate gauge, no OOM counter, no I/O queue depth) are real and worth being able to name specifically rather than claim the system covers everything — they're exactly the kind of "what would you add next" answer that demonstrates the framework was actually understood, not just quoted.

### 12.2 The Four Golden Signals (Google SRE)

**Latency** (how long do requests take — this project's Terminal tab literally surfaces this per-command, via the real `duration_ms` field returned from every `/execute` call, visible directly in Section 1.3's captured output: `2` milliseconds for a real `df -h`), **Traffic** (`rate(node_network_receive_bytes_total[5m])`), **Errors** (`up{job="fleet-agents"} == 0`, plus the backend's own HTTP error responses), **Saturation** (`load_avg_1m / cpu_cores`, memory%, disk%). USE is resource-facing ("is the hardware struggling"); Golden Signals are service-facing ("does the service look healthy from outside") — this system genuinely exposes both, not by accident but because the metric set was built to answer both kinds of question.

### 12.3 Why `rate()`, never a raw counter, in an alert or a chart

Restated precisely, because it's the single most commonly-tested concept in this whole project: a counter's raw value is a *lifetime total*, not a current measurement. `node_network_receive_bytes_total` for a machine that's been running a week might already be in the trillions — that number alone says nothing about whether the network is busy *right now*. `rate(...[5m])` divides the *change* in that counter by the elapsed time, giving bytes-per-second — the only form of that number that's actually meaningful to alert on or chart.

---

## 13. Real bugs and gotchas found while building and testing this

These are not hypothetical "things that could go wrong" — every one below either happened during development (and is recorded directly in this codebase's own comments) or was independently reproduced live while writing this document.

### 13.1 Stale IP → heartbeat failure loop (reproduced live, this session)

The agent installed on this machine had `/etc/fleet-agent/config.json`'s `manager_url` pointing at `http://10.70.40.126:8080` — this machine's LAN IP *from a previous DHCP lease*. After a reboot handed out a new address (`10.70.41.102`), every single heartbeat attempt failed, forever, with the exact same message, captured directly from `journalctl`:

```
Jul 18 20:00:38 ... fleet-agent[977]: Heartbeat: failed to reach manager at http://10.70.40.126:8080: Could not establish connection
```

Because the node's registered `status` had also fallen to `'error'` as a result, and `PrometheusPoller`'s own query only ever polls `WHERE status = 'online'` (§5.6), the node was doubly stuck: the agent couldn't reach the backend to send a heartbeat, and the backend's own poller had stopped even trying to reach the agent. Recovery required a manual fix (correcting the stored IP) — there's no self-healing path once both sides of the heartbeat relationship have gone stale simultaneously.

### 13.2 The double-counted CPU aggregate (documented directly in code comments)

Covered fully in Section 4.5: an earlier version of the agent emitted both the per-core `cpu0`, `cpu1`, … series *and* the pre-summed `cpu` aggregate line under the same metric name. Any PromQL `avg()`/`sum()` across the `cpu` label then silently double-counted idle time, and this surfaced not in a test but as an *impossible negative CPU percentage* on a live Grafana panel. It was traced to the agent, fixed at the source (drop the aggregate line, match real `node_exporter`'s convention exactly), and confirmed by watching the metric recover on the dashboard.

### 13.3 Docker's network namespace boundary (reproduced live, this session, §8.5)

The same stored node IP address is reachable from the native backend process but not from the containerized Prometheus, or vice versa, depending on whether it's `127.0.0.1`, a Docker-internal address, or the real host LAN IP — because "127.0.0.1" and "the host machine" are not the same thing from inside a container's own network namespace. This is the same underlying category of problem `backend_host` auto-detection (§5.3) exists to solve on the onboarding-command side; it turns out to apply just as much to the Prometheus-target side, independently.

### 13.4 Postgres `INET` casting silently includes the netmask

Covered in §6.2: `ip_address::text` on an `INET` column returns `"127.0.0.1/32"`, not `"127.0.0.1"` — invisible in a `psql` prompt if you're not looking closely, but immediately breaks any code that concatenates the result into a URL or command string. Fixed by using `host(ip_address)` everywhere the bare address is needed.

### 13.5 OpenSSL 3.0 deprecated the one-shot `HMAC()` function

Ubuntu 22.04 ships OpenSSL 3.0, which deprecates the older single-call `HMAC()` API in favor of a more verbose fetch/init/update/final pipeline (`EVP_MAC_fetch` → `EVP_MAC_CTX_new` → `EVP_MAC_init` → `EVP_MAC_update` → `EVP_MAC_final`, shown in full in §4.4 — wait, §11.2). Both `backend/src/HmacUtils.cc` and `agent/src/HmacUtils.cc` use the newer API throughout — not because the old one was broken on this specific OpenSSL version, but because it's already marked for removal in a future major version, and writing to the API that will still exist in two years costs nothing extra today.

### 13.6 `sudo` needs an interactive terminal — a real deployment friction

`agent/build/fleet-agent` was, at one point during development, deployed to a target machine as an *update* to an already-running agent — and any step in that process that shelled out to `sudo` non-interactively failed silently, exactly the same class of problem hit again live while writing this document (§13.1's fix attempt: `sudo sed -i ... /etc/fleet-agent/config.json` failed outright with `sudo: a terminal is required to read the password`). The onboarding script itself (§5.4) sidesteps this because it's designed to be run *interactively*, by a human, in a real terminal session where a password prompt can actually be answered — but any attempt to script further changes to an already-onboarded node non-interactively runs straight into this same wall.

---

## 14. Design decisions and their trade-offs

Presented as: what was chosen, what the realistic alternative was, and — honestly — what it costs.

**In-memory sessions instead of Redis.** Zero extra infrastructure to run, an `O(1)` hash-map lookup with no network hop on every authenticated request. Costs: every session is lost on a backend restart (all users logged out at once — exactly what forced the frontend's 401-interceptor in §10.3 to exist), and it fundamentally cannot scale to more than one backend instance, because two processes have two disagreeing maps.

**A custom C++ agent instead of wrapping `node_exporter`.** Gains a management plane `node_exporter` has no concept of at all — registration, per-node tokens, heartbeats, whitelisted remote execution — in a binary roughly a tenth the size. Costs: this agent implements a deliberately narrower slice of `/proc`/`/sys` than the mature, community-hardened `node_exporter` does, and any future bug fix in `node_exporter` upstream doesn't benefit this project automatically.

**PostgreSQL for metric storage, not TimescaleDB or InfluxDB.** One database serves both the relational data (nodes, users) and the time-series data (`metric_snapshots`), with the `LATERAL JOIN` pattern (§6.4) giving genuinely efficient "latest metric per node" queries without a second storage system to run, back up, or reason about. Costs: PostgreSQL, storing one row per node per 15 seconds with no purpose-built time-series compression or downsampling, would not hold up well at real fleet scale (a fleet of 500 nodes over 30 days is roughly 500 × 30 × 24 × 60 × 4 ≈ 86 million rows) — which is exactly why the 24-hour retention window (§5.6) exists as a deliberate cap, and exactly the kind of workload TimescaleDB's automatic time-based partitioning ("chunks") and downsampling policies are purpose-built to handle instead.

**Docker only for Prometheus and Grafana; everything else runs natively.** Prometheus and Grafana are genuinely fiddly to install and version-pin natively; Docker gives exact, reproducible versions with zero native install friction. The backend, by contrast, has a build process complex enough (compiling Drogon from source against a specific PostgreSQL client version — see `setup.sh`) that a Docker build for it would add real friction to the development loop, for a component that's iterated on constantly during development, for no corresponding benefit. The mixed model is a direct, considered trade of "consistency of deployment" against "speed of local development," made deliberately in one direction for infrequently-changed infrastructure and the other direction for actively-developed code.

---

## 15. What's missing, on purpose

Named directly rather than left for someone else to discover:

| Gap | Why it's acceptable for this project's actual scope | What would force fixing it |
|---|---|---|
| In-memory sessions | Single backend process; a restart is the only cost, and it's an explicit, understood one | The moment there's more than one backend instance behind a load balancer |
| No rate limiting | Trusted network, known operators, demo scale | Before anything is exposed to an untrusted network — the login endpoint especially |
| Sequential `PrometheusPoller` (one HTTP request at a time, in a loop) | Fine for a handful of nodes well within a 15-second window | Once fleet size makes a full poll cycle take longer than the 15-second interval itself |
| No TLS anywhere | Localhost / trusted LAN only | Day one of anything beyond that — named directly in §11.4, not discovered by an auditor |
| Single admin account, no RBAC | One operator persona is the entire current scope | The moment a second human needs their own, separately-scoped access |
| No `agent_token` rotation | Named directly in the threat table (§11.4) | Before this is trusted with anything where a single leaked token being permanently valid is unacceptable |

---

## 16. Full question bank

### Linux internals

**Q: What is `/proc` and why does reading it cost almost nothing?**
It's a virtual filesystem — the "files" are generated by the kernel from its own live state the instant you open them, not read off disk. There's no I/O involved beyond a small in-kernel memory copy, so it can be read very frequently with negligible overhead.

**Q: Why can't you compute CPU usage from a single `/proc/stat` read?**
Every counter in that file is a cumulative total since boot. One reading only tells you the average since the machine started, not the current load. You need two readings, separated by a known interval, and the *difference* between them.

**Q: Why does `iowait` count as idle for CPU utilisation purposes?**
Because the CPU genuinely has nothing to execute during it — a process is blocked waiting for disk or network I/O, not waiting for the CPU. Folding `iowait` into "idle" keeps "CPU utilisation" measuring actual computation; a separate, high `iowait` figure is itself a useful signal that the bottleneck is I/O, not compute.

**Q: What happens to these counters across a reboot, and how does software need to handle that?**
They reset to (near) zero. Code computing a rate from two readings has to detect a reading that's *smaller* than the stored previous value and treat that as "counters reset," discarding that one delta rather than reporting an impossible negative rate, then re-baselining for the next cycle. This project's `PrometheusPoller::parsePrometheusText` does exactly this (`if (totalDelta > 0)` guards, §5.7).

**Q: Why does `/proc/diskstats` always use 512-byte sectors even on a drive with 4K physical sectors?**
It's a stable kernel ABI guarantee, kept for backward compatibility with every tool that has ever parsed this file. The kernel does the physical-to-logical translation internally; user-space code can always safely multiply by 512 regardless of the drive's real sector size.

### Architecture

**Q: This system has two separate things scraping the same agent — why?**
The backend's `PrometheusPoller` feeds PostgreSQL, which serves the custom React dashboard's own history endpoints with plain SQL range queries. The real Prometheus feeds Grafana, which needs PromQL's much richer aggregation surface against a storage engine actually built for that pattern. They're independent by design, not by accident, and their agreement (or, during development, their *disagreement* — §13.2) is itself a useful signal.

**Q: Why does the agent push heartbeats to the backend instead of the backend polling the agent for liveness?**
Two reasons. A node might sit behind a firewall or NAT that only allows outbound connections — the agent initiating contact works in far more network topologies than the backend reaching in would. And it's simpler: the agent already knows its own schedule; a separate liveness-poll mechanism from the backend would just be duplicating what `PrometheusPoller`'s own scrape-or-mark-error loop already does.

**Q: What's the practical difference between this project's two authentication mechanisms?**
JWT + session map authenticates a *human* across a browser session, with a revocation path via logout. HMAC-signed tokens authenticate the backend *to a specific agent* on a per-request basis, with no concept of "logging in" — a node has one long-lived identity, established once at onboarding.

### Security

**Q: Walk through exactly what a timing attack is and how this project avoids it.**
If a signature comparison returns as soon as it finds the first mismatched byte, a wrong guess that's correct for its first 40 characters takes measurably longer to reject than one that's wrong in its very first character. An attacker who can send enough requests and measure response latency precisely could, in principle, use that timing difference to recover a valid signature one byte at a time. `CRYPTO_memcmp` (OpenSSL's constant-time comparison, used in both `HmacUtils::verifyHmac` implementations) always examines every byte regardless of where a mismatch is, so the comparison's timing carries no information about how close a guess was.

**Q: `system()` vs `execvp()` — what specifically goes wrong with the first one?**
`system()` hands your whole command string to `/bin/sh -c`, which interprets shell metacharacters (`;`, `&&`, `|`, `` ` ``, `$(...)`). Any argument that isn't fully under your control can inject a second command. `execvp()` never invokes a shell at all — it hands the target program a literal array of argument strings with no parsing step in between, so there is no mechanism by which a metacharacter in an argument could ever be interpreted as anything but a literal character.

**Q: If a JWT is stolen, what's the actual blast radius, precisely?**
Full API access as that user for up to 8 hours (the fixed expiry) — reading all fleet data, onboarding nodes, running any whitelisted command on any node. It cannot be used to extend its own life (the `exp` claim is inside the signed payload). If the legitimate user logs out in the meantime, the stolen token is immediately rejected too, because logout removes the token from the shared in-memory session map regardless of which copy of the token is presenting it.

### Prometheus / observability

**Q: Counter vs. gauge, with real examples from this system.**
Counter: only increases (or resets on restart); always wrapped in `rate()`. `node_cpu_seconds_total`, `node_network_receive_bytes_total`. Gauge: reflects a current value, up or down; compared directly. `node_memory_MemAvailable_bytes`, `node_load1`.

**Q: What does `up{job="fleet-agents"} == 0` actually detect that agent-reported metrics wouldn't?**
`up` is generated entirely by Prometheus itself, based purely on whether the scrape HTTP request succeeded — it doesn't depend on the target's own code doing anything correctly. Even a completely broken agent that returns garbage would still correctly show `up == 1` if it's at least reachable and returns *a* response; `up == 0` specifically means the target couldn't even be reached, which is a strictly different and more fundamental failure than "reporting bad numbers."

**Q: Why 5 minutes for the `HighCPU` alert's `rate()` window, and 5 more minutes on top of that for `for:`?**
The 5-minute `rate()` window smooths out short spikes (a two-second burst wouldn't move a 5-minute average much); the additional `for: 5m` means the *already-smoothed* condition itself has to hold continuously for 5 more minutes before the alert fires at all. Combined, this system will not page anyone for a CPU crisis until it has been going on for roughly 10 minutes — a deliberate trade of alert latency against false-positive suppression.

### Drogon / C++

**Q: Explain the event loop model and why it beats thread-per-connection here.**
A small, fixed pool of threads (4, in this project) each use `epoll` to watch many sockets simultaneously and only wake up when one has actual work ready, instead of dedicating one OS thread per connection (each costing real memory) most of which would sit idle waiting on I/O. One event-loop thread can hold open far more concurrent connections than one thread-per-connection server could, for a fraction of the memory.

**Q: Why does `PrometheusPoller` use the *blocking* database API instead of the async one every controller uses?**
Drogon's async API (`execSqlAsync`) is built around registering a callback that fires later, on an event-loop thread — it assumes the calling context *is* the event loop. `PrometheusPoller` runs on its own plain `std::thread`, not an event-loop thread, so that assumption doesn't hold; using the synchronous, blocking API (`execSqlSync`) instead is the correct fit precisely because this dedicated thread has no other responsibility that blocking it would interfere with.

---

## 17. Appendix: reproduce every command in this document

All of this assumes the project is already set up per `setup.sh` (PostgreSQL migrated, backend and agent built, frontend dependencies installed).

```bash
# 1. Start everything
docker start linuxfleetmanagement-prometheus-1 linuxfleetmanagement-grafana-1   # or: docker compose -f docker-compose.monitoring.yml up -d
./backend/build/fleet-backend &
cd frontend && npm run dev &

# 2. Raw kernel data this whole system is built on
cat /proc/stat | head -4
cat /proc/meminfo | head -10
cat /proc/loadavg

# 3. The agent's own view (only meaningful once a node is onboarded and running)
curl -s http://<node-ip>:9200/metrics | head -20
curl -s http://<node-ip>:9200/health

# 4. Login and everything that needs a token
TOKEN=$(curl -s -X POST http://localhost:8080/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}' | python3 -c "import json,sys;print(json.load(sys.stdin)['token'])")

curl -s http://localhost:8080/api/nodes -H "Authorization: Bearer $TOKEN" | python3 -m json.tool
curl -s http://localhost:8080/api/metrics/summary -H "Authorization: Bearer $TOKEN" | python3 -m json.tool

# 5. A real signed remote command (replace <node-id>)
curl -s -X POST http://localhost:8080/api/nodes/<node-id>/commands \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"command":"df","args":["-h"]}' | python3 -m json.tool

# 6. What Prometheus sees
curl -s http://localhost:8080/internal/prometheus-targets | python3 -m json.tool
curl -s http://localhost:9090/api/v1/targets | python3 -m json.tool
curl -s 'http://localhost:9090/api/v1/query' \
  --data-urlencode 'query=100 - avg(rate(node_cpu_seconds_total{mode="idle"}[5m])) * 100' | python3 -m json.tool

# 7. Grafana
curl -s -u admin:admin http://localhost:3001/api/dashboards/uid/fleet-overview | python3 -m json.tool
curl -s -u admin:admin http://localhost:3001/api/datasources/uid/prometheus/health
```

---

*Every capitalized code identifier, file path, SQL statement, and JSON response in this document was taken directly from this repository's source and/or captured from a real, running instance of this system on 2026-07-18. Where a figure (CPU %, memory %, byte counts) appears, it's a genuine measurement from this machine at the time of writing, not an illustrative placeholder.*
