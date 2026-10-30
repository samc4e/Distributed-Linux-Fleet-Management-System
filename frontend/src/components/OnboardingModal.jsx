import { useState, useEffect } from 'react';
import { createNode, getNode } from '../api';

export default function OnboardingModal({ onClose, onSuccess }) {
  const [hostname, setHostname] = useState('');
  const [ipAddress, setIpAddress] = useState('');
  const [agentPort, setAgentPort] = useState('9200');
  const [labelsText, setLabelsText] = useState('');
  const [step, setStep] = useState(1);
  const [result, setResult] = useState(null);
  const [error, setError] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [copied, setCopied] = useState(false);
  const [nodeStatus, setNodeStatus] = useState('pending');

  // Poll the node's own status while step 2 is showing so the operator sees
  // it flip from "pending" to "online" without leaving the modal.
  useEffect(() => {
    if (step !== 2 || !result) {
      return undefined;
    }
    const interval = setInterval(async () => {
      try {
        const res = await getNode(result.node_id);
        setNodeStatus(res.data.status);
      } catch (err) {
        // Ignore transient polling errors; next tick will retry.
      }
    }, 3000);
    return () => clearInterval(interval);
  }, [step, result]);

  const handleSubmit = async (e) => {
    e.preventDefault();
    setError('');

    if (!hostname.trim() || !ipAddress.trim()) {
      setError('Hostname and IP address are required');
      return;
    }

    let labels = {};
    if (labelsText.trim()) {
      try {
        labels = JSON.parse(labelsText);
      } catch (err) {
        setError('Labels must be valid JSON, e.g. {"env": "prod"}');
        return;
      }
    }

    setSubmitting(true);
    try {
      const res = await createNode({
        hostname: hostname.trim(),
        ip_address: ipAddress.trim(),
        agent_port: parseInt(agentPort, 10) || 9200,
        labels,
      });
      setResult(res.data);
      setStep(2);
    } catch (err) {
      setError(err.response?.data?.error || 'Failed to create node');
    } finally {
      setSubmitting(false);
    }
  };

  const handleCopy = () => {
    if (result) {
      navigator.clipboard.writeText(result.onboarding_command);
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    }
  };

  const handleClose = () => {
    onClose();
    if (step === 2) {
      onSuccess();
    }
  };

  return (
    <div className="modal-overlay" onClick={handleClose}>
      <div className="modal" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <h2>{step === 1 ? 'Add Node' : 'Node Created'}</h2>
          <button className="modal-close" onClick={handleClose}>
            &times;
          </button>
        </div>

        {step === 1 && (
          <form onSubmit={handleSubmit}>
            <label htmlFor="hostname">Hostname</label>
            <input
              id="hostname"
              value={hostname}
              onChange={(e) => setHostname(e.target.value)}
              placeholder="web-01"
              autoFocus
            />

            <label htmlFor="ipAddress">IP Address</label>
            <input
              id="ipAddress"
              value={ipAddress}
              onChange={(e) => setIpAddress(e.target.value)}
              placeholder="192.168.1.100"
            />

            <label htmlFor="agentPort">Agent Port</label>
            <input id="agentPort" value={agentPort} onChange={(e) => setAgentPort(e.target.value)} placeholder="9200" />

            <label htmlFor="labels">Labels (JSON, optional)</label>
            <input
              id="labels"
              value={labelsText}
              onChange={(e) => setLabelsText(e.target.value)}
              placeholder='{"env": "prod"}'
            />

            {error && <div className="error-message">{error}</div>}

            <div className="modal-actions">
              <button type="button" className="secondary" onClick={handleClose}>
                Cancel
              </button>
              <button type="submit" disabled={submitting}>
                {submitting ? 'Creating...' : 'Create Node'}
              </button>
            </div>
          </form>
        )}

        {step === 2 && result && (
          <div>
            <p>Node created! Run this command on the target machine:</p>
            <div className="onboard-command">
              <pre>{result.onboarding_command}</pre>
              <button className="secondary copy-btn" onClick={handleCopy}>
                {copied ? 'Copied!' : 'Copy to clipboard'}
              </button>
            </div>
            <div className="waiting-indicator">
              {nodeStatus === 'online' ? (
                <span style={{ color: 'var(--green)' }}>Node is online!</span>
              ) : (
                <>
                  <span className="spinner" />
                  Waiting for agent heartbeat...
                </>
              )}
            </div>
            <div className="modal-actions">
              <button onClick={handleClose}>Close</button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
