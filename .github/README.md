# Fleet Manager

A web-based Linux fleet management system. An operator adds a node in the browser, runs one shell command on the target machine, and within ~30 seconds that machine shows up online with live CPU/memory/disk/network metrics in this dashboard and in Grafana via Prometheus.

This is an academic demonstration project: the priority is a correct, complete, working implementation of the core flow above, not production hardening. See [Architecture decisions](#architecture-decisions) for the tradeoffs that follow from that.

## Table of contents

- [Fleet Manager](#fleet-manager)
  - [Table of contents](#table-of-contents)
  - [Architecture](#architecture)
  - [Prerequisites](#prerequisites)
  - [Setup](#setup)
  - [Demo walkthrough](#demo-walkthrough)
  - [API reference](#api-reference)
    - [Backend](#backend)
    - [Agent (runs on each managed node, default port 9200)](#agent-runs-on-each-managed-node-default-port-9200)
  - [Architecture decisions](#architecture-decisions)
  - [Observability principles](#observability-principles)
    - [USE Method (Utilization / Saturation / Errors)](#use-method-utilization--saturation--errors)
    - [Four golden signals](#four-golden-signals)
    - [Prometheus naming convention](#prometheus-naming-convention)
    - [Backend self-metrics](#backend-self-metrics)
  - [Repository layout](#repository-layout)

## Architecture

```
                      Browser
                         |
                         | HTTP
                         v
               React Frontend (Vite :5173)
                         |
                         | REST + JWT (Bearer token)
                         v
          +------ Drogon Backend (:8080) ------+
          |         C++17, callback API         |
          |                                     |
          | POST /api/agent/heartbeat (30s)     | GET /metrics (every 15s)
          | agent_token auth                    | stores metric_snapshots row
          v                                     v
          +------------ fleet-agent(s) ---------+
                    one per managed node
                    reads /proc directly
                    :9200 (default)
                             ^
                             | GET /metrics (direct scrape, 15s)
                             | target list from
                             | GET /internal/prometheus-targets
                  +----------+----------+
                  |      Prometheus      |
                  |    Docker :9090      |
                  +----------+----------+
                             |
                             | PromQL queries
                             v
                  +---------------------+
                  |       Grafana        |
                  |    Docker :3001      |
                  |  Fleet Overview      |
                  |  (auto-provisioned)  |
                  +---------------------+
```

Backend, frontend, and PostgreSQL run natively on the host. Only Prometheus and Grafana run in Docker (`docker-compose.monitoring.yml`). That is the only Docker usage in this project.

## Prerequisites

- Ubuntu 22.04 (or close enough: anything with apt, a recent g++, and systemd)
- Docker + Docker Compose plugin, for Prometheus and Grafana only
- sudo access, for installing packages and PostgreSQL setup
- Internet access, to fetch Drogon source and two header-only libraries

## Setup

```bash
./setup.sh
```

This installs system packages, builds Drogon 1.9.12 from source (5-10 minutes, apt does not carry a recent enough package), fetches jwt-cpp and cpp-httplib, creates the `fleet` Postgres role and database, runs the three migrations, and builds the backend, agent, and frontend deps. The script is idempotent, so re-running it after a partial failure is safe.

The backend auto-detects this machine's LAN IP at startup (skipping loopback and Docker's virtual interfaces) and uses it to build the onboarding command. No manual config is needed even after a DHCP lease change. To override this, set `backend_host` in `backend/config.json` explicitly. A non-empty value always takes precedence. The backend logs which address it used on startup:

```
fleet-backend: backend_host not set in config.json, auto-detected LAN IP 192.168.1.42
# or
fleet-backend: using backend_host override from config.json: 10.0.0.5
```

## Demo walkthrough

**1. Start the monitoring stack**

```bash
docker compose -f docker-compose.monitoring.yml up -d
```

**2. Start the backend**

```bash
./backend/build/fleet-backend
```

**3. Start the frontend**

```bash
cd frontend && npm run dev
```

**4. Open the dashboard**

Navigate to http://localhost:5173 and log in as `admin` / `admin123`.

**5. Add a node**

Go to Nodes and click `+ Add Node`. Fill in a hostname and IP address. For a same-machine demo use `127.0.0.1`. After submitting you get back a command like:

```bash
AGENT_TOKEN=... MANAGER_URL=http://127.0.0.1:8080 bash <(curl -fsSL http://127.0.0.1:8080/onboard.sh)
```

**6. Run the onboarding command**

Run it on the target machine (or a second terminal on this one). It downloads the compiled agent from the backend, writes `/etc/fleet-agent/config.json`, and installs and starts it as a systemd service.

**7. Watch the node come online**

Within ~30 seconds (one heartbeat interval) the node flips from *Pending* to *Online* in both the modal and the dashboard, with real CPU/memory/OS/hardware info. Metric charts populate over the next couple of poll cycles (15s each).

**8. Run a remote command**

Open the node's Terminal or Filesystem tab and run a whitelisted command: `df`, `free`, `ps`, `uptime`, `netstat`, `ss`, `lscpu`, or `uname`. This round-trips through the backend to the agent over an HMAC-signed request.

**9. Open Grafana**

Open the Grafana tab in the node detail page, or go to http://localhost:3001 directly (admin / admin). The Fleet Overview dashboard is auto-provisioned and shows the same data via Prometheus.

> **Docker networking note.** Prometheus runs in a container and scrapes agents using the IP address you registered the node with. If you register a node as `127.0.0.1`, that resolves to Prometheus's own container when Prometheus tries to scrape it, not your host. Prometheus will show that target as down even though the app's own dashboard (which the backend scrapes from the host side) shows it online correctly. For a demo where Prometheus and Grafana also need to reach the agent, register the node with the host's real LAN IP instead of `127.0.0.1`. The `fleet-backend` job does not have this problem since it is scraped via `host.docker.internal`.

## API reference

All `/api/*` routes except login require `Authorization: Bearer <token>`.

### Backend

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| POST | `/api/auth/login` | none | `{username, password}` returns JWT + user |
| POST | `/api/auth/logout` | JWT | Invalidate the current session |
| GET | `/api/auth/me` | JWT | Current user |
| GET | `/api/nodes` | JWT | All nodes with latest metric snapshot |
| POST | `/api/nodes` | JWT | Register a node, returns onboarding command |
| GET | `/api/nodes/{id}` | JWT | Node detail with last 100 snapshots |
| PATCH | `/api/nodes/{id}` | JWT | Update hostname or labels |
| DELETE | `/api/nodes/{id}` | JWT | Decommission (soft delete, sets status to offline) |
| POST | `/api/nodes/{id}/commands` | JWT | Run a whitelisted command on the node |
| GET | `/api/metrics/summary` | JWT | Fleet-wide counts and averages |
| GET | `/api/metrics/node/{id}?minutes=N` | JWT | Metric history for the last N minutes |
| POST | `/api/agent/heartbeat` | agent_token in body | Agent liveness and system info |
| GET | `/internal/prometheus-targets` | source-IP allowlist | Prometheus HTTP SD target list |
| GET | `/onboard.sh` | none | Installer script text |
| GET | `/agent/download` | none | Compiled agent binary |
| GET | `/metrics` | none | Backend's own Prometheus self-metrics |

### Agent (runs on each managed node, default port 9200)

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/metrics` | none | Prometheus exposition of this node's stats |
| GET | `/health` | none | `{"status":"ok","version":"1.0.0"}` |
| POST | `/execute` | X-Agent-Signature HMAC | Run a whitelisted command |

## Architecture decisions

**Why a C++ agent.** It reads `/proc` and computes rates with essentially zero overhead and no runtime dependency beyond a couple of shared libraries. This is appropriate for something that runs continuously on every managed node, including nodes where you do not want to install a language runtime.

**Why Drogon.** A mature, actively-maintained native C++ web framework with an ORM that talks directly to libpq, an HTTP client for outbound agent calls, and enough AOP hooks (`registerSyncAdvice`, `registerPreSendingAdvice`) to implement CORS without a full filter or plugin system. No interpreter, no GC pause, predictable latency.

**Why parse `/proc` directly instead of shelling out to `top` or `vmstat`.** `/proc` is the kernel's own accounting data. Parsing it directly avoids a process fork per collection cycle and avoids depending on the text formatting of tools that vary across distros and kernel versions.

**Why `execvp`, never `system()` or a shell.** `CommandRunner::execute` builds an argv array and calls `execvp` directly. The whitelisted command and its arguments never pass through `/bin/sh`. There is no shell metacharacter for an attacker to inject because there is no shell in the loop at all.

**Why Prometheus HTTP service discovery instead of a static target list.** `GET /internal/prometheus-targets` is generated from the live `nodes` table, so Prometheus's scrape targets stay in sync automatically instead of needing a config reload every time a node is onboarded.

**Why in-memory sessions instead of Redis.** This is a single-process demo backend. An `unordered_map` behind a mutex is simpler to reason about and sufficient at this scale. The real cost is explicit and worth knowing: restarting the backend invalidates every session. The frontend axios interceptor handles this by redirecting to `/login` on a 401 rather than getting stuck. A production deployment with multiple backend instances would need a shared session store.

**Why the onboarding script installs a systemd unit.** `Restart=always` means an agent that crashes or a node that reboots comes back on its own, without the operator needing to know it happened.

**Why auto-detect `backend_host` instead of requiring it in config.json.** The onboarding command embeds this address so a target node knows where to phone home. A stale hardcoded value after a DHCP lease change fails silently as "no route to host". `main.cc` walks `getifaddrs()` for the first non-loopback, non-Docker `AF_INET` interface at startup. `backend_host` in `config.json` becomes an explicit override for the rare case where auto-detection picks the wrong interface on a multi-homed machine.

**One deviation worth flagging.** Drogon's Postgres client takes a single `connectionNumber` for its pool, not separate min and max values. That is a real constraint of the library, not an oversight. `main.cc` sets it to 10 (the specified max) with a comment explaining why.

## Observability principles

### USE Method (Utilization / Saturation / Errors)

| Resource | Utilization | Saturation | Errors |
|----------|-------------|------------|--------|
| CPU | `100 - rate(node_cpu_seconds_total{mode="idle"}[5m])*100` | `node_load1` vs core count (`hardware_info.cpu_cores`) | `mode="steal"` (time stolen by a hypervisor) |
| Memory | `1 - node_memory_MemAvailable_bytes/node_memory_MemTotal_bytes` | SwapTotal minus SwapFree | not exposed (no OOM-kill counter in this demo) |
| Disk | `rate(node_disk_io_time_seconds_total[5m])` | queue depth not exposed | not exposed |
| Network | `rate(node_network_{receive,transmit}_bytes_total[5m])` | not exposed | `node_network_{receive,transmit}_errs_total` |

### Four golden signals

- **Latency.** Every agent command result carries `duration_ms`.
- **Traffic.** Network byte and packet rates per interface. Fleet-wide request volume is not separately instrumented on the backend beyond its own `/metrics` endpoint.
- **Errors.** `*_errs_total` network counters. The backend and agent both return explicit non-2xx codes with a JSON `{"error": "..."}` body rather than swallowing failures.
- **Saturation.** CPU percent, memory percent, load averages, disk I/O time.

### Prometheus naming convention

Every agent metric follows `node_<subsystem>_<name>_<unit>`, with a `_total` suffix reserved for monotonic counters (`node_cpu_seconds_total`, `node_network_receive_bytes_total`) and no suffix for gauges (`node_load1`, `node_procs_running`). This matches the convention `node_exporter` itself uses, which means the alert rules and Grafana panels in this repo would also work unmodified against a real `node_exporter` if you swap this agent out for one.

### Backend self-metrics

`GET /metrics` on the backend exposes `fleet_backend_up` and `fleet_backend_uptime_seconds` so the `fleet-backend` Prometheus job has something real to scrape. It is intentionally minimal rather than a full self-instrumentation layer.

## Repository layout

| Directory | Contents |
|-----------|----------|
| `backend/` | Drogon C++17 API server |
| `agent/` | cpp-httplib C++17 agent (runs on each managed node) |
| `frontend/` | React + Vite dashboard |
| `migrations/` | Plain .sql files, run once by setup.sh |
| `prometheus/` | Scrape config and alert rules (mounted into the container) |
| `grafana/` | Provisioned datasource and Fleet Overview dashboard |