import { useState, useEffect, useCallback } from 'react';
import { useNavigate } from 'react-router-dom';
import Navbar from '../components/Navbar';
import StatusBadge from '../components/StatusBadge';
import { getFleetSummary, getNodes } from '../api';

export default function DashboardPage() {
  const [summary, setSummary] = useState(null);
  const [nodes, setNodes] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const navigate = useNavigate();

  const load = useCallback(async () => {
    try {
      const [summaryRes, nodesRes] = await Promise.all([getFleetSummary(), getNodes()]);
      setSummary(summaryRes.data);
      setNodes(nodesRes.data);
      setError('');
    } catch (err) {
      setError('Failed to load dashboard data');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    load();
    const interval = setInterval(load, 15000);
    return () => clearInterval(interval);
  }, [load]);

  const hotNodes = nodes.filter((n) => n.cpu_percent != null && n.cpu_percent > 85);

  return (
    <div className="page">
      <Navbar />
      <div className="content">
        <h1>Dashboard</h1>

        {loading && <p className="loading-text">Loading...</p>}
        {error && <div className="error-message">{error}</div>}

        {hotNodes.length > 0 && (
          <div className="alert-banner">
            WARNING: {hotNodes.length} node{hotNodes.length > 1 ? 's' : ''} with CPU &gt; 85%
          </div>
        )}

        {summary && (
          <div className="stat-grid">
            <div className="stat-card">
              <div className="label">Total Nodes</div>
              <div className="value">{summary.total}</div>
            </div>
            <div className="stat-card">
              <div className="label">Online</div>
              <div className="value green">{summary.online}</div>
            </div>
            <div className="stat-card">
              <div className="label">Offline</div>
              <div className="value red">{summary.offline}</div>
            </div>
            <div className="stat-card">
              <div className="label">Avg CPU</div>
              <div className="value blue">{summary.avg_cpu_percent.toFixed(1)}%</div>
            </div>
          </div>
        )}

        <h2>Node Status</h2>
        {!loading && nodes.length === 0 ? (
          <div className="empty-state">No nodes yet. Add one from the Nodes page.</div>
        ) : (
          <table>
            <thead>
              <tr>
                <th>Hostname</th>
                <th>IP Address</th>
                <th>Status</th>
                <th>CPU %</th>
                <th>Memory %</th>
                <th>Last Seen</th>
              </tr>
            </thead>
            <tbody>
              {nodes.map((node) => {
                const memPct =
                  node.mem_total_bytes && node.mem_used_bytes != null
                    ? ((node.mem_used_bytes / node.mem_total_bytes) * 100).toFixed(1)
                    : 'N/A';
                return (
                  <tr key={node.id} className="clickable" onClick={() => navigate(`/nodes/${node.id}`)}>
                    <td>{node.hostname}</td>
                    <td>{node.ip_address}</td>
                    <td>
                      <StatusBadge status={node.status} />
                    </td>
                    <td>{(node.cpu_percent || 0).toFixed(1)}</td>
                    <td>{memPct === 'N/A' ? memPct : `${memPct}%`}</td>
                    <td>{node.last_seen ? new Date(node.last_seen).toLocaleString() : 'Never'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}
