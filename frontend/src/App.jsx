import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom';
import { isAuthenticated } from './auth';
import LoginPage from './pages/LoginPage';
import DashboardPage from './pages/DashboardPage';
import NodesPage from './pages/NodesPage';
import NodeDetailPage from './pages/NodeDetailPage';

function PrivateRoute({ children }) {
  return isAuthenticated() ? children : <Navigate to="/login" replace />;
}

export default function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/login" element={<LoginPage />} />
        <Route
          path="/dashboard"
          element={
            <PrivateRoute>
              <DashboardPage />
            </PrivateRoute>
          }
        />
        <Route
          path="/nodes"
          element={
            <PrivateRoute>
              <NodesPage />
            </PrivateRoute>
          }
        />
        <Route
          path="/nodes/:id"
          element={
            <PrivateRoute>
              <NodeDetailPage />
            </PrivateRoute>
          }
        />
        <Route path="/" element={<Navigate to="/dashboard" replace />} />
      </Routes>
    </BrowserRouter>
  );
}
