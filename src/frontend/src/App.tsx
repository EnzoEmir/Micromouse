import { useState } from 'react';
import { TelemetriaPage } from './pages/TelemetriaPage';
import MazeViewer from './components/maze/MazeViewer';
import Session from './components/Session';

function App() {
  const [currentView, setCurrentView] = useState<'session' | 'telemetria'>('session');

  return (
    <main className="app">
      {currentView === 'session' ? (
        <Session onNavigate={() => setCurrentView('telemetria')} />
      ) : (
        <>
          <TelemetriaPage />
          <MazeViewer />
        </>
      )}
    </main>
  );
}

export default App;