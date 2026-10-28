import { ResponsiveContainer, AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip } from 'recharts';

function formatTime(value) {
  const d = new Date(value);
  if (Number.isNaN(d.getTime())) return '';
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

export default function MemoryChart({ data }) {
  if (!data || data.length === 0) {
    return (
      <div className="chart-card">
        <h3>Memory Usage</h3>
        <div className="chart-empty">No data yet</div>
      </div>
    );
  }

  const chartData = data.map((d) => ({
    collected_at: d.collected_at,
    mem_percent:
      d.mem_total_bytes && d.mem_used_bytes != null ? (d.mem_used_bytes / d.mem_total_bytes) * 100 : 0,
  }));

  return (
    <div className="chart-card">
      <h3>Memory Usage</h3>
      <ResponsiveContainer width="100%" height={220}>
        <AreaChart data={chartData}>
          <CartesianGrid strokeDasharray="3 3" stroke="#2a323e" />
          <XAxis dataKey="collected_at" tickFormatter={formatTime} stroke="#8b96a5" fontSize={12} />
          <YAxis
            domain={[0, 100]}
            stroke="#8b96a5"
            fontSize={12}
            label={{ value: 'Memory %', angle: -90, position: 'insideLeft', fill: '#8b96a5' }}
          />
          <Tooltip
            labelFormatter={formatTime}
            formatter={(value) => [`${Number(value).toFixed(1)}%`, 'Memory']}
            contentStyle={{ background: '#161c24', border: '1px solid #2a323e' }}
          />
          <Area type="monotone" dataKey="mem_percent" stroke="#00ff88" fill="#00ff88" fillOpacity={0.25} />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}
