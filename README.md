# Fleet Manager

A web-based Linux fleet management system. An operator adds a node in the
browser, runs one shell command on the target machine, and within ~30
seconds that machine shows up online with live CPU/memory/disk/network
metrics — in this app's own dashboard, and in Grafana via Prometheus.

This is an academic demonstration project: the priority is a correct,
complete, working implementation of the core flow above, not production
hardening (see [Architecture decisions](#architecture-decisions) for the
trade-offs that follow from that).

## Architecture

```
                          ┌───────────────────┐
                          │     Browser        │
                          └─────────┬──────────┘
                                    │ HTTP
                                    ▼
                          ┌───────────────────┐
                          │  React Frontend    │
                          │  Vite dev server    │
                          │     :5173           │
                          └─────────┬──────────┘
                                    │ REST + JWT (Bearer token)
                                    ▼
                          ┌───────────────────┐         ┌──────────────────┐
                          │  Drogon Backend     │◄───────►│   PostgreSQL      │
                          │  C++17, callback API│         │   nodes,           │
                          │     :8080            │         │   metric_snapshots │
                          └──┬────────────┬─────┘         └──────────────────┘
                             │            │
     POST /api/agent/heartbeat (30s)      │ GET /metrics, scrapes every 15s,
     agent_token auth                     │ stores a metric_snapshots row
                             │            │ per node per cycle
                             ▼            ▼
                       ┌───────────────────────┐
                       │   fleet-agent(s)        │
                       │   one per managed node   │
                       │   reads /proc directly    │
                       │   :9200 (default)          │
                       └───────────┬───────────────┘
                                   ▲
                                   │ GET /metrics (direct scrape, 15s)
                                   │ target list from
                                   │ GET /internal/prometheus-targets
                    ┌──────────────┴───────────────┐
                    │         Prometheus              │
                    │      Docker, :9090               │
                    └──────────────┬───────────────────┘
                                   │ PromQL queries
                                   ▼
                    ┌───────────────────────────────┐
                    │            Grafana               │
                    │        Docker, :3001              │
                    │   "Fleet Overview" dashboard        │
                    └───────────────────────────────────┘
```

Backend, frontend, and PostgreSQL run natively on the host. Only Prometheus
and Grafana run in Docker (`docker-compose.monitoring.yml`) — that's the
only Docker usage in the project.

## Prerequisites

- Ubuntu 22.04 (or close enough — anything with apt, a recent g++, and
  systemd works)
- Docker + Docker Compose plugin, for Prometheus/Grafana only
- sudo access, for installing packages and PostgreSQL setup
- Internet access, to fetch Drogon's source and two header-only libraries

## Setup

```bash
./setup.sh
```

This installs system packages, builds Drogon 1.9.12 from source (~5-10
minutes — apt doesn't carry a recent-enough Drogon package), fetches
jwt-cpp and cpp-httplib, creates the `fleet` Postgres role/database and
runs the three migrations, and builds the backend, agent, and frontend
deps. It's idempotent, so re-running it after a partial failure is fine.

The backend auto-detects this machine's LAN IP at startup (skipping
loopback and Docker's own virtual interfaces) and uses it to build the
onboarding command, so no manual config is needed even after a reboot
changes your DHCP lease. To force a specific address instead — e.g. a
multi-homed machine where auto-detection picks the wrong interface — set
`backend_host` in `backend/config.json` explicitly; a non-empty value
always overrides auto-detection. The backend logs which one it used on
startup (`fleet-backend: backend_host not set in config.json, auto-detected
LAN IP ...` vs `using backend_host override from config.json: ...`).

## Demo walkthrough

1. **Start the monitoring stack:**
   ```bash
   docker compose -f docker-compose.monitoring.yml up -d
   ```
2. **Start the backend:**
   ```bash
   ./backend/build/fleet-backend
   ```
3. **Start the frontend:**
   ```bash
   cd frontend && npm run dev
   ```
4. **Open** http://localhost:5173, log in as `admin` / `admin123`.
5. **Nodes → + Add Node.** Fill in a hostname and IP address (for a
   same-machine demo, use `127.0.0.1`) and submit. You'll get back a
   command like:
   ```
   AGENT_TOKEN=... MANAGER_URL=http://127.0.0.1:8080 bash <(curl -fsSL http://127.0.0.1:8080/onboard.sh)
   ```
6. **Run that command** on the target machine (or a second terminal on
   this one). It downloads the compiled agent from the backend, writes
   `/etc/fleet-agent/config.json`, and installs+starts it as a systemd
   service.
7. Within ~30 seconds (one heartbeat interval) the node flips from
   *Pending* to *Online* in both the modal and the dashboard, with real
   CPU/memory/OS/hardware info. Metrics charts populate over the next
   couple of poll cycles (15s each).
8. Open the node's **Terminal** or **Filesystem** tab and run a whitelisted
   command (`df`, `free`, `ps`, `uptime`, `netstat`, `ss`, `lscpu`,
   `uname`) — this round-trips through the backend to the agent over an
   HMAC-signed request.
9. Open the **Grafana** tab, or http://localhost:3001 directly
   (admin/admin) — the "Fleet Overview" dashboard (auto-provisioned) shows
   the same data via Prometheus.

**Docker networking note:** Prometheus runs in a container and scrapes
agents using the IP address you registered the node with. If you register
a node as `127.0.0.1` for an all-on-one-machine demo, that resolves to
*Prometheus's own container* when Prometheus tries to scrape it, not your
host — so Prometheus will show that target as down even though the app's
own dashboard (which the backend scrapes from the host side) shows it
online correctly. For a demo where Prometheus/Grafana also need to reach
the agent, register the node with the host's real LAN IP instead of
`127.0.0.1`. The `fleet-backend` job doesn't have this problem since it's
scraped via `host.docker.internal`.

## API reference

All `/api/*` routes except login require `Authorization: Bearer <token>`.

| Method | Path | Auth | Description |
|---|---|---|---|
| POST | `/api/auth/login` | — | `{username, password}` → JWT + user |
| POST | `/api/auth/logout` | JWT | Invalidate the current session |
| GET | `/api/auth/me` | JWT | Current user |
| GET | `/api/nodes` | JWT | All nodes + latest metric snapshot |
| POST | `/api/nodes` | JWT | Register a node → onboarding command |
| GET | `/api/nodes/{id}` | JWT | Node detail + last 100 snapshots |
| PATCH | `/api/nodes/{id}` | JWT | Update hostname/labels |
| DELETE | `/api/nodes/{id}` | JWT | Decommission (soft delete → offline) |
| POST | `/api/nodes/{id}/commands` | JWT | Run a whitelisted command on the node |
| GET | `/api/metrics/summary` | JWT | Fleet-wide counts + averages |
| GET | `/api/metrics/node/{id}?minutes=N` | JWT | Metric history for the last N minutes |
| POST | `/api/agent/heartbeat` | agent_token in body | Agent liveness + system info |
| GET | `/internal/prometheus-targets` | source-IP allowlist | Prometheus HTTP SD target list |
| GET | `/onboard.sh` | — | Installer script text |
| GET | `/agent/download` | — | Compiled agent binary |
| GET | `/metrics` | — | Backend's own Prometheus self-metrics |

Agent (runs on each managed node, default port 9200):

| Method | Path | Auth | Description |
|---|---|---|---|
| GET | `/metrics` | — | Prometheus exposition of this node's stats |
| GET | `/health` | — | `{"status":"ok","version":"1.0.0"}` |
| POST | `/execute` | `X-Agent-Signature` HMAC | Run a whitelisted command |

## Architecture decisions

**Why a C++ agent.** It reads `/proc` and computes rates with essentially
zero overhead and no runtime dependency beyond a couple of shared
libraries — appropriate for something that runs continuously on every
managed node, including nodes you don't want to install a language runtime
on.

**Why Drogon.** A mature, actively-maintained native-C++ web framework
with an ORM that talks directly to libpq, an HTTP client for outbound
agent calls, and enough AOP hooks (`registerSyncAdvice`,
`registerPreSendingAdvice`) to implement CORS without a full filter/plugin
system. No interpreter, no GC pause, predictable latency.

**Why parse `/proc` directly instead of shelling out to `top`/`vmstat`.**
`/proc` *is* the kernel's own accounting data — parsing it directly avoids
a process fork per collection cycle and avoids depending on the text
formatting of tools that vary across distros/versions.

**Why `execvp`, never `system()` or a shell.** `CommandRunner::execute`
builds an argv array and calls `execvp` directly; the whitelisted command
and its arguments never pass through `/bin/sh`. There is no shell
metacharacter for an attacker to inject because there is no shell in the
loop at all.

**Why Prometheus HTTP service discovery instead of a static target list.**
The fleet changes as nodes are added — `GET
/internal/prometheus-targets` is generated from the live `nodes` table, so
Prometheus's scrape targets stay in sync automatically instead of needing
a config reload every time a node is onboarded.

**Why in-memory sessions instead of Redis.** This is a single-process demo
backend; an `unordered_map` behind a mutex is simpler to reason about and
sufficient at this scale. The real cost is explicit and worth knowing:
restarting the backend invalidates every session (the frontend's axios
interceptor handles this by redirecting to `/login` on a 401 rather than
getting stuck). A production deployment with multiple backend instances
would need a shared session store.

**Why the onboarding script installs a systemd unit.** `Restart=always`
means an agent that crashes or a node that reboots comes back on its own,
without the operator needing to know that happened.

**Why auto-detect `backend_host` instead of requiring it in config.json.**
The onboarding command embeds this address so a target node knows where to
phone home — a stale hardcoded value after a DHCP lease change (common on
a laptop moving between networks or just rebooting) fails as a silent
`No route to host`, not a clear config error, the first time you try to
onboard. `main.cc` walks `getifaddrs()` for the first non-loopback,
non-Docker `AF_INET` interface at startup instead; `backend_host` in
config.json becomes an explicit override for the rare case (multi-homed
box) where auto-detection guesses wrong, rather than something you must
maintain by hand.

**One deviation worth flagging:** Drogon's Postgres client takes a single
`connectionNumber` for its pool, not the separate min/max the brief
describes — that's a real constraint of the library, not an oversight, so
`main.cc` sets it to 10 (the specified max) with a comment explaining why.

## Observability principles

**USE Method** (Utilization / Saturation / Errors) — what each resource's
metrics cover:

| Resource | Utilization | Saturation | Errors |
|---|---|---|---|
| CPU | `100 - rate(node_cpu_seconds_total{mode="idle"}[5m])*100` | `node_load1` vs core count (`hardware_info.cpu_cores`) | `mode="steal"` (time stolen by a hypervisor) |
| Memory | `1 - node_memory_MemAvailable_bytes/node_memory_MemTotal_bytes` | `node_memory_SwapTotal_bytes - node_memory_SwapFree_bytes` | not exposed (no OOM-kill counter in this demo) |
| Disk | `rate(node_disk_io_time_seconds_total[5m])` | queue depth not exposed | not exposed |
| Network | `rate(node_network_{receive,transmit}_bytes_total[5m])` | — | `node_network_{receive,transmit}_errs_total` |

**Four Golden Signals:**
- *Latency* — every agent command result carries `duration_ms`.
- *Traffic* — network byte/packet rates per interface; fleet-wide request
  volume isn't separately instrumented on the backend beyond its own
  `/metrics` (see "not stubbed, but minimal" note below).
- *Errors* — `*_errs_total` network counters; the backend and agent both
  return explicit non-2xx codes with a JSON `{"error": "..."}` body rather
  than swallowing failures.
- *Saturation* — CPU%, memory%, load averages, disk I/O time.

**Prometheus naming convention.** Every agent metric follows
`node_<subsystem>_<name>_<unit>`, with a `_total` suffix reserved for
monotonic counters (`node_cpu_seconds_total`,
`node_network_receive_bytes_total`) and no suffix for gauges
(`node_load1`, `node_procs_running`) — matching the convention
`node_exporter` itself uses, which is why the alert rules and Grafana
panels in this repo would also work unmodified against a real
`node_exporter` if you ever swapped this agent out for one.

**Backend self-metrics.** `GET /metrics` on the backend exposes
`fleet_backend_up` and `fleet_backend_uptime_seconds` so the
`fleet-backend` Prometheus job (declared in `prometheus/prometheus.yml`)
has something real to scrape instead of a permanently-down target — it's
intentionally minimal rather than a full self-instrumentation layer, which
would be scope beyond what this brief asks for.

## Repository layout

```
backend/     Drogon C++17 API server
agent/       cpp-httplib C++17 agent (runs on each managed node)
frontend/    React + Vite dashboard
migrations/  Plain .sql files, run once by setup.sh
prometheus/  Scrape config + alert rules (mounted into the container)
grafana/     Provisioned datasource + "Fleet Overview" dashboard
```

Walkthrough
https://claude.ai/code/artifact/49173c1b-800d-486a-b9f6-c7c4c8871c80

Runbook
https://claude.ai/code/artifact/2dc6c0e6-ae07-41df-b98d-97b0dddca720

## Author & Maintainer

samc4e (Sameer Chore) — 2024bcs011@sggs.ac.in
