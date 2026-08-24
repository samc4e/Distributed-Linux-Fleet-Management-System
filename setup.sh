#!/bin/bash
# One-shot setup for the Fleet Manager demo on Ubuntu 22.04.
#
# Installs system dependencies, builds Drogon from source (the apt package
# archives don't carry 1.9.x), fetches the two header-only C++ libraries,
# initializes PostgreSQL, and builds the backend, agent, and frontend.
#
# Safe to re-run: every step is idempotent (skips work that's already done).
set -e

cd "$(dirname "$0")"

echo "=== Installing system dependencies ==="
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git \
  libssl-dev libpq-dev libjsoncpp-dev uuid-dev zlib1g-dev \
  postgresql postgresql-contrib \
  curl nodejs npm

echo "=== Installing Drogon (v1.9.12, built from source) ==="
if [ ! -d "/tmp/drogon" ]; then
  git clone --depth 1 --branch v1.9.12 https://github.com/drogonframework/drogon /tmp/drogon
fi
cd /tmp/drogon
git submodule update --init
mkdir -p build && cd build
cmake .. \
  -DBUILD_POSTGRESQL=ON \
  -DBUILD_MYSQL=OFF \
  -DBUILD_SQLITE3=OFF \
  -DBUILD_REDIS=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
sudo make install
sudo ldconfig
cd - > /dev/null

cd "$(dirname "$0")"

echo "=== Fetching header-only dependencies ==="
mkdir -p backend/third_party agent/third_party

# jwt-cpp (header-only). The upstream repo nests its headers under
# include/jwt-cpp/; we flatten that one level so `#include "jwt-cpp/jwt.h"`
# resolves against `include_directories(third_party)` in CMakeLists.txt.
if [ ! -f "backend/third_party/jwt-cpp/jwt.h" ]; then
  rm -rf /tmp/jwt-cpp-src
  git clone --depth 1 https://github.com/Thalhammer/jwt-cpp /tmp/jwt-cpp-src
  mkdir -p backend/third_party/jwt-cpp
  cp -r /tmp/jwt-cpp-src/include/jwt-cpp/. backend/third_party/jwt-cpp/
  rm -rf /tmp/jwt-cpp-src
fi

# cpp-httplib (header-only)
if [ ! -f "agent/third_party/httplib.h" ]; then
  curl -fsSL https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
    -o agent/third_party/httplib.h
fi

echo "=== Setting up PostgreSQL ==="
sudo -u postgres psql -c "CREATE USER fleet WITH PASSWORD 'fleetpass';" 2>/dev/null || true
sudo -u postgres psql -c "CREATE DATABASE fleetdb OWNER fleet;" 2>/dev/null || true
for f in migrations/*.sql; do
  echo "  running $f"
  PGPASSWORD=fleetpass psql -h 127.0.0.1 -U fleet -d fleetdb -v ON_ERROR_STOP=1 -f "$f"
done
echo "Database initialized (admin / admin123)."

echo "=== Building backend ==="
cd backend
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
cd ../..

echo "=== Building agent ==="
cd agent
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
cd ../..

echo "=== Installing frontend dependencies ==="
cd frontend
npm install
cd ..

echo ""
echo "=== Setup complete ==="
echo ""
echo "Edit backend/config.json first if this machine's LAN IP isn't"
echo "127.0.0.1 - set backend_host so onboarding commands work from other"
echo "machines, and allowed_prometheus_cidr if Prometheus is not on localhost."
echo ""
echo "To start the system:"
echo "  1. Start monitoring stack:  docker compose -f docker-compose.monitoring.yml up -d"
echo "  2. Start backend:           ./backend/build/fleet-backend"
echo "  3. Start frontend:          cd frontend && npm run dev"
echo "  4. Open browser:            http://localhost:5173"
echo "  5. Login:                   admin / admin123"
echo "  6. Prometheus:              http://localhost:9090"
echo "  7. Grafana:                 http://localhost:3001 (admin/admin)"
echo ""
echo "To run the agent on THIS machine (for demo):"
echo "  sudo mkdir -p /etc/fleet-agent"
echo "  sudo cp agent/build/fleet-agent /usr/local/bin/"
echo "  # Create /etc/fleet-agent/config.json with the token shown after"
echo "  # clicking 'Add Node' in the UI, then:"
echo "  /usr/local/bin/fleet-agent"
echo ""
echo "Or simplest: click 'Add Node' in the UI and run the onboarding"
echo "command it gives you directly on the target machine - it downloads"
echo "the agent from the backend and installs it as a systemd service."
