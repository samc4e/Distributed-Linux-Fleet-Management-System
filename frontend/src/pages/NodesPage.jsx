import { useState, useEffect, useCallback } from 'react';
import { useNavigate } from 'react-router-dom';
import Navbar from '../components/Navbar';
import NodeCard from '../components/NodeCard';
import OnboardingModal from '../components/OnboardingModal';
import { getNodes } from '../api';

const FILTERS = ['all', 'online', 'offline', 'pending'];

export default function NodesPage() {
  const [nodes, setNodes] = useState([]);
  const [loading, setLoading] = useState(true);
  const [showModal, setShowModal] = useState(false);
  const [filter, setFilter] = useState('all');
  const navigate = useNavigate();

  const load = useCallback(async () => {
    try {
      const res = await getNodes();
      setNodes(res.data);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    load();
    const interval = setInterval(load, 15000);
    return () => clearInterval(interval);
  }, [load]);

  const filtered = filter === 'all' ? nodes : nodes.filter((n) => n.status === filter);

  return (
    <div className="page">
      <Navbar />
      <div className="content">
        <div className="page-header">
          <h1>Fleet Nodes</h1>
          <button onClick={() => setShowModal(true)}>+ Add Node</button>
        </div>

        <div className="filter-bar">
          {FILTERS.map((f) => (
            <button key={f} className={filter === f ? 'active' : ''} onClick={() => setFilter(f)}>
              {f.charAt(0).toUpperCase() + f.slice(1)}
            </button>
          ))}
        </div>

        {loading && <p className="loading-text">Loading...</p>}

        {!loading && filtered.length === 0 && <div className="empty-state">No nodes match this filter.</div>}

        <div className="node-grid">
          {filtered.map((node) => (
            <NodeCard key={node.id} node={node} onClick={() => navigate(`/nodes/${node.id}`)} />
          ))}
        </div>
      </div>

      {showModal && (
        <OnboardingModal
          onClose={() => setShowModal(false)}
          onSuccess={() => {
            setShowModal(false);
            load();
          }}
        />
      )}
    </div>
  );
}
