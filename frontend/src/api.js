import axios from 'axios';
import { getToken, setToken } from './auth';

const BASE = import.meta.env.VITE_API_URL || 'http://localhost:8080';

const api = axios.create({ baseURL: BASE });

// Attach JWT to every request.
api.interceptors.request.use((config) => {
  const t = getToken();
  if (t) {
    config.headers.Authorization = `Bearer ${t}`;
  }
  return config;
});

// The backend keeps sessions in memory (see JwtMiddleware), so a backend
// restart invalidates every token at once. Bounce back to the login screen
// instead of leaving the UI stuck replaying failed requests.
api.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response && error.response.status === 401) {
      setToken(null);
      if (window.location.pathname !== '/login') {
        window.location.href = '/login';
      }
    }
    return Promise.reject(error);
  }
);

export const login = (username, password) => api.post('/api/auth/login', { username, password });

export const logout = () => api.post('/api/auth/logout');

export const getMe = () => api.get('/api/auth/me');

export const getNodes = () => api.get('/api/nodes');
export const getNode = (id) => api.get(`/api/nodes/${id}`);
export const createNode = (data) => api.post('/api/nodes', data);
export const updateNode = (id, data) => api.patch(`/api/nodes/${id}`, data);
export const deleteNode = (id) => api.delete(`/api/nodes/${id}`);
export const execCommand = (id, command, args) =>
  api.post(`/api/nodes/${id}/commands`, { command, args });

export const getFleetSummary = () => api.get('/api/metrics/summary');
export const getNodeMetrics = (id, minutes = 60) =>
  api.get(`/api/metrics/node/${id}?minutes=${minutes}`);

export default api;
