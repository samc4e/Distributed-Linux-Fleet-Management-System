# Fleet Manager — Live Demo Runbook

Every command, in order, to go from a cold laptop to a fresh AWS node reporting live metrics in the dashboard and Grafana.

**Ports:** backend `:8080` · frontend `:5173` · agent `:9200` · Prometheus `:9090` · Grafana `:3001`

---

## 0. Pre-flight — what already exists (one-time, done already)

| Thing | Where | Status |
|---|---|---|
| SSH key | `~/Fleetdemo.pem` | `chmod 400` applied |
| Backend binary | `backend/build/fleet-backend` | built |
| Agent binary | `agent/build/fleet-agent` | built, x86_64 |
| Postgres | systemd service `postgresql` | auto-starts on boot |
| DB credentials | `backend/config.json` | `fleet` / `fleetpass` / `fleetdb` |
| Login | seeded in `users` table | `admin` / `admin123` |
| Tailscale on laptop | already joined tailnet | stable IP, doesn't change |

---

## 1. Launch or resume the EC2 instance

If you kept the instance **stopped** (not terminated), it keeps its Tailscale identity and IP — just start it. If terminated, launch fresh with the same spec.

- EC2 console → Instances → if stopped: **Instance state → Start**. Wait ~20s, note the new **public IPv4** (changes every start unless you attached an Elastic IP).
- If launching fresh: AMI Ubuntu Server 22.04 LTS (x86_64) · Type t3.micro · Security group: inbound SSH (22) from My IP only · Key pair: Fleetdemo.

SSH in to confirm it's up:

```bash
ssh -i ~/Fleetdemo.pem ubuntu@<EC2_PUBLIC_IP>
```

---

## 2. Bring up local services (on your laptop)

Postgres and Docker usually survive a reboot on their own — the Prometheus/Grafana **containers do not auto-restart**. This is what broke the Grafana tab last time.

```bash
systemctl is-active postgresql
```

```bash
cd ~/Desktop/Dev/linuxFleetManagement
docker compose -f docker-compose.monitoring.yml up -d
```

Verify: `docker ps` should show both containers as `Up`, not `Exited`.

---

## 3. Tailscale — connect the EC2 box

Skip this whole phase if it's the same instance as last time. Your laptop is already on the tailnet permanently.

On the EC2 box (via SSH):

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

Open the printed login URL in a browser, approve under the same Tailscale account.

Confirm both machines are on the tailnet:

```bash
tailscale status
```

You should see two `linux` entries — your laptop and the EC2 hostname (e.g. `ip-172-31-x-x`). Note the EC2 box's `100.x.y.z` address.

---

## 4. Point the backend at your Tailscale IP (one-time — verify only)

Get your laptop's Tailscale IP:

```bash
tailscale ip -4
```

Set it in `backend/config.json` (already set from last session — only touch if wrong/empty):

```json
"backend_host": "<YOUR_TAILSCALE_IP>",
```

Start the backend (restart required after any config.json edit):

```bash
cd ~/Desktop/Dev/linuxFleetManagement/backend/build
./fleet-backend
```

Start the frontend, in a separate terminal:

```bash
cd ~/Desktop/Dev/linuxFleetManagement/frontend
npm run dev
```

Open `http://localhost:5173` and log in with `admin` / `admin123`.

---

## 5. Clean the database for a fresh demo (optional but recommended)

Wipes old demo nodes (`aws-ec2-demo`, `demo-123`) and their metric history so the dashboard starts empty. The `users` table (your login) is untouched — `metric_snapshots` cascades automatically via its foreign key.

> **Warning:** this permanently deletes every registered node and its history. Only run it right before a demo, not mid-session.

Check what's currently registered:

```bash
PGPASSWORD=fleetpass psql -h localhost -U fleet -d fleetdb \
  -c "SELECT hostname, ip_address, status FROM nodes;"
```

Delete all nodes (cascades to metric_snapshots):

```bash
PGPASSWORD=fleetpass psql -h localhost -U fleet -d fleetdb \
  -c "DELETE FROM nodes;"
```

Confirm empty:

```bash
PGPASSWORD=fleetpass psql -h localhost -U fleet -d fleetdb \
  -c "SELECT count(*) FROM nodes;"
```

Refresh the Dashboard page — Total Nodes should read 0.

---

## 6. Onboard the EC2 node

1. Frontend → Nodes → Add Node. Hostname: anything descriptive (e.g. `aws-ec2-demo`). IP address: the EC2 box's **Tailscale IP** from step 3 (the `100.x.y.z` one) — not its AWS public IP.
2. Copy the generated onboarding command — it already embeds a fresh `AGENT_TOKEN` and the correct `MANAGER_URL` pointing at your Tailscale IP.
3. On the EC2 box (SSH session), paste and run it:

```bash
# example shape — use the real one the UI gives you
AGENT_TOKEN=<token> MANAGER_URL=http://<laptop_tailscale_ip>:8080 \
  bash <(curl -fsSL http://<laptop_tailscale_ip>:8080/onboard.sh)
```

4. Wait ~30 seconds — the agent's first heartbeat flips the node to **Online** automatically.

---

## 6A. Onboarding an on-site machine (same WiFi) — primary path

Use this when the interviewer's own bare-metal machine is physically present and on the same WiFi as your laptop. No Tailscale, no AWS — plain LAN.

1. Confirm both devices show the same WiFi network name.
2. Switch the backend to LAN mode in `backend/config.json`:

```json
"backend_host": "",
```

3. Restart the backend (config is only read at startup):

```bash
cd ~/Desktop/Dev/linuxFleetManagement/backend/build
./fleet-backend
```

4. Sanity-check what it auto-detected — this is the IP their machine needs to reach:

```bash
hostname -I
```

5. If your firewall is active, make sure it's not blocking inbound 8080 from the LAN:

```bash
sudo ufw status
sudo ufw allow 8080/tcp    # only if ufw shows "active" and 8080 isn't already allowed
```

6. Ask them for their machine's LAN IP (they run this on their box):

```bash
hostname -I
```

7. Optional sanity check before generating the command:

```bash
ping -c 2 <their_lan_ip>
```

8. Frontend → Nodes → Add Node → hostname (anything descriptive) + their LAN IP as `ip_address`.
9. Copy the generated onboarding command and hand it to them (read it aloud, paste in chat, or type it directly on their machine — it's just a shell one-liner, no file transfer involved).
10. They run it in their terminal. Wait ~30 seconds — node flips to **Online**, and because this is plain LAN, Metrics/Terminal/Grafana all work immediately (backend can reach their agent directly, both directions).

---

## 6B. Fallback — if the same-WiFi path doesn't work

This happens when the WiFi has **client isolation** enabled (common on corporate, hotel, or guest networks) — devices on the same SSID can't reach each other directly even though they're "connected." Step 7 above (`ping`) is what catches this: if it times out, isolation is on and 6A cannot work no matter what you change in the app.

**Don't reach for Tailscale Funnel here** — it only fixes traffic in one direction (their machine calling out to you), which is enough for the onboarding script itself to run, but the backend still couldn't call back into their isolated machine afterward, so Metrics/Terminal/Grafana would stay broken for that node. Not worth the confusion mid-demo.

**Do this instead — add their machine to your Tailscale network.** This fixes both directions regardless of WiFi isolation, because traffic goes over the Tailscale mesh, not the local WiFi at all. It's the exact same mechanism already proven working with the AWS box.

1. In the Tailscale admin console, generate a one-off ephemeral auth key (Settings → Keys → Generate auth key — reusable, short expiry is fine for a single demo).
2. On their machine:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up --authkey=<the_key_you_generated>
```

3. Confirm it joined:

```bash
tailscale status
```

You should now see their machine listed alongside your laptop and the EC2 box.

4. Set `backend_host` back to your **Tailscale IP** (not LAN):

```json
"backend_host": "100.94.177.30",
```

5. Restart the backend, get their new Tailscale IP (`100.x.y.z`) from `tailscale status`, then repeat steps 8-10 from 6A using that IP instead of their LAN IP.

> Note: the agent binary was rebuilt this session with TLS support (`CPPHTTPLIB_OPENSSL_SUPPORT`) so it can speak HTTPS if you ever do need a Funnel-style public URL for something one-directional in the future — but for onboarding a fully-working node, Tailscale-join (above) is the correct fallback, not Funnel.

---

## 7. Verify end to end

Walk this once, silently, before the call — it catches the two things that have gone wrong before (stopped Grafana container, stale Tailscale IP).

| Check | Where | Expect |
|---|---|---|
| Node status | Dashboard page | Online, green dot |
| Live metrics | Node detail → Metrics tab | CPU / Memory charts moving |
| Remote exec | Node detail → Terminal tab | `exit 0` on `free` or `df -h` |
| Prometheus targets | `curl localhost:9090/api/v1/targets` | all 3 targets `"health":"up"` |
| Grafana embed | Node detail → Grafana tab | panel renders, not blank |

```bash
curl -s http://localhost:9090/api/v1/targets | python3 -m json.tool | grep health
curl -s http://localhost:3001/api/health
```

---

## 8. Stress test — make the charts move on cue

Run these **on the EC2 instance directly** via SSH, not through the app's terminal tab (that's a locked-down read-only whitelist by design).

Install stress-ng (once per instance):

```bash
sudo apt-get update && sudo apt-get install -y stress-ng
```

CPU spike — both cores, 60s, auto-stops:

```bash
stress-ng --cpu 2 --timeout 60s
```

Memory spike — ~75% of RAM, 60s:

```bash
stress-ng --vm 1 --vm-bytes 75% --timeout 60s
```

Both together — good single demo moment:

```bash
stress-ng --cpu 2 --vm 1 --vm-bytes 75% --timeout 90s
```

> Give it 15-30s to appear — that's the agent's collection interval. t3.micro is burstable (CPU credits); a 60-90s burst is completely safe, just don't loop it for many minutes back to back right before the call.
>
> To actually trip the `HighCPU` / `HighMemory` Prometheus alert rules (5-minute sustained threshold), use `--timeout 300s` instead.

---

## 9. After the interview — cost control

Stop the EC2 instance (don't terminate) — keeps its Tailscale identity for next time. EC2 console → Instance state → Stop.

Optionally stop local containers:

```bash
docker compose -f docker-compose.monitoring.yml stop
```

t3.micro is roughly $0.01/hr on-demand — a few hours of demo prep and the real call cost cents. Stopping (not terminating) between now and the interview avoids re-running Tailscale setup and losing the instance's IP identity.


If encounter problems then ==>
Different root cause this time — "stuck on pending" means the agent never sent even its first heartbeat, so this is happening before onboarding even fully succeeds, unlike the VirtualBox case. Two Fedora-specific things are almost certainly at play, since Fedora behaves very differently from Ubuntu here: SELinux (enforcing by default, Ubuntu has none) and firewalld (active by default, Ubuntu's ufw is inactive by default).

Most likely cause: SELinux is blocking the binary from executing. The onboarding script downloads to /tmp then sudo mvs it to /usr/local/bin. mv preserves the file's SELinux context — so the binary keeps the tmp_t label from /tmp instead of getting relabeled to something executable-by-services like bin_t. SELinux then blocks systemd from ever running it, the service fails silently (or crash-loops under Restart=always), and no heartbeat ever fires.

Run this on the Fedora machine to confirm and fix:

sudo systemctl status fleet-agent --no-pager
sudo journalctl -u fleet-agent -n 30 --no-pager

If that shows it failed to start / is crash-looping, confirm it's SELinux:

getenforce
sudo ausearch -m avc -ts recent 2>/dev/null | tail -20

You're likely to see an avc: denied { execute } line referencing tmp_t. Fix is one command, safe to run regardless:

sudo restorecon -v /usr/local/bin/fleet-agent
sudo systemctl restart fleet-agent
sudo systemctl status fleet-agent --no-pager

That should flip it to active (running), and the node should go online within ~30s.

Second thing to pre-empt on Fedora: even once the heartbeat direction works, the scrape direction (backend → their :9200) will likely hit the same "online but no metrics" issue we just fixed on the VirtualBox box — except this time it'd be firewalld, not a NAT boundary. Fedora Workstation ships with firewalld active by default, blocking inbound ports it doesn't know about. Open it now so you don't have to debug it twice:

sudo firewall-cmd --state
sudo firewall-cmd --add-port=9200/tcp --permanent
sudo firewall-cmd --reload

Run the systemctl status / journalctl commands first and tell me what they show — I want to confirm it's actually SELinux before you go further, in case it's something else entirely (e.g. the onboarding curl itself never completed).