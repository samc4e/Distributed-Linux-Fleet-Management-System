import { useState, useEffect, useCallback } from 'react';
import { useParams } from 'react-router-dom';
import Navbar from '../components/Navbar';
import StatusBadge from '../components/StatusBadge';
import CpuChart from '../components/CpuChart';
import MemoryChart from '../components/MemoryChart';
import NetworkChart from '../components/NetworkChart';
import LoadChart from '../components/LoadChart';
import { getNode, getNodeMetrics, execCommand } from '../api';

const TERMINAL_COMMANDS = ['df', 'free', 'ps', 'uptime', 'netstat', 'uname', 'lscpu'];
const TIME_RANGES = [
  { label: '1h', minutes: 60 },
  { label: '6h', minutes: 360 },
  { label: '24h', minutes: 1440 },
];

export default function NodeDetailPage() {
  const { id } = useParams();
  const [node, setNode] = useState(null);
  const [metrics, setMetrics] = useState([]);
  const [activeTab, setActiveTab] = useState('metrics');
  const [loading, setLoading] = useState(true);
  const [timeRangeMinutes, setTimeRangeMinutes] = useState(60);

  const [fsOutput, setFsOutput] = useState(null);
  const [fsLoading, setFsLoading] = useState(false);

  const [selectedCommand, setSelectedCommand] = useState('df');
  const [commandResult, setCommandResult] = useState(null);
  const [commandLoading, setCommandLoading] = useState(false);
  const [commandHistory, setCommandHistory] = useState([]);

  const loadNode = useCallback(async () => {
    try {
      const res = await getNode(id);
      setNode(res.data);
      if (res.data.recent_metrics) {
        setMetrics(res.data.recent_metrics);
      }
    } finally {
      setLoading(false);
    }
  }, [id]);

  const loadMetrics = useCallback(async () => {
    try {
      const res = await getNodeMetrics(id, timeRangeMinutes);
      setMetrics(res.data);
    } catch (err) {
      // Keep showing the last known metrics on a transient failure.
    }
  }, [id, timeRangeMinutes]);

  // Initial load, and whenever the node id in the URL changes.
  useEffect(() => {
    loadNode();
  }, [loadNode]);

  // Metrics refresh on its own cadence, and re-runs immediately when the
  // time range selector changes.
  useEffect(() => {
    loadMetrics();
    const interval = setInterval(loadMetrics, 15000);
    return () => clearInterval(interval);
  }, [loadMetrics]);

  // Keep the header (status/last_seen) fresh independently of the metrics
  // range.
  useEffect(() => {
    const interval = setInterval(loadNode, 15000);
    return () => clearInterval(interval);
  }, [loadNode]);

  const runFilesystemCheck = async () => {
    setFsLoading(true);
    try {
      const res = await execCommand(id, 'df', ['-h']);
      setFsOutput(res.data);
    } catch (err) {
      setFsOutput({
        stdout: '',
        stderr: err.response?.data?.error || 'Command failed',
        exit_code: -1,
        duration_ms: 0,
      });
    } finally {
      setFsLoading(false);
    }
  };

  const runTerminalCommand = async () => {
    setCommandLoading(true);
    try {
      const res = await execCommand(id, selectedCommand, []);
      setCommandResult(res.data);
      setCommandHistory((prev) => [{ command: selectedCommand, ...res.data }, ...prev].slice(0, 10));
    } catch (err) {
      const failure = {
        stdout: '',
        stderr: err.response?.data?.error || 'Command failed',
        exit_code: -1,
        duration_ms: 0,
      };
      setCommandResult(failure);
      setCommandHistory((prev) => [{ command: selectedCommand, ...failure }, ...prev].slice(0, 10));
    } finally {
      setCommandLoading(false);
    }
  };

  if (loading) {
    return (
      <div className="page">
        <Navbar />
        <div className="content">
          <p className="loading-text">Loading...</p>
        </div>
      </div>
    );
  }

  if (!node) {
    return (
      <div className="page">
        <Navbar />
        <div className="content">
          <p className="loading-text">Node not found.</p>
        </div>
      </div>
    );
  }

  return (
    <div className="page">
      <Navbar />
      <div className="content">
        <div className="node-header">
          <div className="node-header-top">
            <h1>{node.hostname}</h1>
            <StatusBadge status={node.status} />
          </div>
          <div className="node-header-meta">
            <div className="group">
              <span className="group-title">IP Address</span>
              <span>{node.ip_address}</span>
            </div>
            <div className="group">
              <span className="group-title">Last Seen</span>
              <span>{node.last_seen ? new Date(node.last_seen).toLocaleString() : 'Never'}</span>
            </div>
            {node.os_info && (
              <div className="group">
                <span className="group-title">OS</span>
                <span>
                  {node.os_info.distro} ({node.os_info.kernel}, {node.os_info.arch})
                </span>
              </div>
            )}
            {node.hardware_info && (
              <div className="group">
                <span className="group-title">Hardware</span>
                <span>
                  {node.hardware_info.cpu_model} &middot; {node.hardware_info.cpu_cores} cores &middot;{' '}
                  {(node.hardware_info.mem_total_bytes / 1024 / 1024 / 1024).toFixed(1)} GB RAM
                </span>
              </div>
            )}
          </div>
        </div>

        <div className="tabs">
          {['metrics', 'filesystem', 'terminal', 'grafana'].map((tab) => (
            <button
              key={tab}
              className={`tab ${activeTab === tab ? 'active' : ''}`}
              onClick={() => setActiveTab(tab)}
            >
              {tab.charAt(0).toUpperCase() + tab.slice(1)}
            </button>
          ))}
        </div>

        {activeTab === 'metrics' && (
          <div>
            <div className="time-range-selector">
              {TIME_RANGES.map((r) => (
                <button
                  key={r.minutes}
                  className={timeRangeMinutes === r.minutes ? 'active' : ''}
                  onClick={() => setTimeRangeMinutes(r.minutes)}
                >
                  {r.label}
                </button>
              ))}
            </div>
            <CpuChart data={metrics} />
            <MemoryChart data={metrics} />
            <NetworkChart data={metrics} />
            <LoadChart data={metrics} />
          </div>
        )}

        {activeTab === 'filesystem' && (
          <div>
            <button onClick={runFilesystemCheck} disabled={fsLoading}>
              {fsLoading ? 'Running...' : 'Run df -h'}
            </button>
            {fsOutput && (
              <div className="command-output" style={{ marginTop: 16 }}>
                <div className="command-output-meta">
                  <span className={`badge ${fsOutput.exit_code === 0 ? 'ok' : 'fail'}`}>
                    exit {fsOutput.exit_code}
                  </span>
                  <span>{fsOutput.duration_ms}ms</span>
                </div>
                <pre>{fsOutput.stdout || fsOutput.stderr}</pre>
              </div>
            )}
          </div>
        )}

        {activeTab === 'terminal' && (
          <div>
            <div className="terminal-controls">
              <div className="field">
                <label htmlFor="cmd-select">Command</label>
                <select id="cmd-select" value={selectedCommand} onChange={(e) => setSelectedCommand(e.target.value)}>
                  {TERMINAL_COMMANDS.map((c) => (
                    <option key={c} value={c}>
                      {c}
                    </option>
                  ))}
                </select>
              </div>
              <button onClick={runTerminalCommand} disabled={commandLoading}>
                {commandLoading ? 'Running...' : 'Execute'}
              </button>
            </div>

            {commandResult && (
              <div className="command-output">
                <div className="command-output-meta">
                  <span className={`badge ${commandResult.exit_code === 0 ? 'ok' : 'fail'}`}>
                    exit {commandResult.exit_code}
                  </span>
                  <span>{commandResult.duration_ms}ms</span>
                </div>
                <pre>{commandResult.stdout || commandResult.stderr}</pre>
              </div>
            )}

            {commandHistory.length > 0 && (
              <div>
                <h3>History</h3>
                <div className="history-list">
                  {commandHistory.map((h, idx) => (
                    <div className="history-item" key={idx}>
                      <div className="history-item-cmd">
                        $ {h.command} &nbsp;
                        <span className={`badge ${h.exit_code === 0 ? 'ok' : 'fail'}`}>exit {h.exit_code}</span>
                      </div>
                      <pre>{h.stdout || h.stderr}</pre>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </div>
        )}

        {activeTab === 'grafana' && (
          <div className="grafana-frame-wrap">
            <iframe
              src="http://localhost:3001/d/fleet-overview/fleet-overview?orgId=1&kiosk"
              style={{ width: '100%', height: 600, border: 'none' }}
              title="Grafana Fleet Overview"
            />
            <p className="grafana-note">
              <a href="http://localhost:3001/d/fleet-overview/fleet-overview?orgId=1" target="_blank" rel="noreferrer">
                Open in Grafana ↗
              </a>
              <br />
              Note: Filter by node using Grafana&apos;s hostname variable.
            </p>
          </div>
        )}
      </div>
    </div>
  );
}
