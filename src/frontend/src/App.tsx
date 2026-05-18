import { useState } from 'react';
import { LabirintoPage } from './pages/LabirintoPage';
import { TelemetriaPage } from './pages/TelemetriaPage';
import Session from './components/Session';

function App() {
  const [currentView, setCurrentView] = useState<'session' | 'telemetria' | 'labirinto'>('session');

  return (
    <main className="app">
      {currentView === 'session' ? (
        <Session onNavigate={() => setCurrentView('telemetria')} />
      ) : currentView === 'telemetria' ? (
        <TelemetriaPage
          activeView={currentView}
          onNavigateTelemetria={() => setCurrentView('telemetria')}
          onNavigateLabirinto={() => setCurrentView('labirinto')}
        />
      ) : (
        <LabirintoPage
          activeView={currentView}
          onNavigateTelemetria={() => setCurrentView('telemetria')}
          onNavigateLabirinto={() => setCurrentView('labirinto')}
        />
      )}
    </main>
  );
}

export default App;