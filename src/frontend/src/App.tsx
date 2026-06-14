import { useState } from 'react';
import { Toaster } from 'react-hot-toast';
import { TelemetriaPage } from './pages/TelemetriaPage';
import { HistoricoCorridasPage } from './pages/HistoricoCorridasPage';
import { EstadosPage } from './pages/EstadosPage';
import Session from './components/Session';

function App() {
  const [currentView, setCurrentView] = useState<
    'session' | 'telemetria' | 'corridas' | 'estados'>('session');

  return (
    <main className="app">
      <Toaster position="top-right" />
      {currentView === 'session' ? (
        <Session onNavigate={() => setCurrentView('telemetria')} />
      ) : currentView === 'telemetria' ? (
        <TelemetriaPage
          activeView={currentView}
          onNavigateTelemetria={() => setCurrentView('telemetria')}
          onNavigateCorridas={() => setCurrentView('corridas')}
          onNavigateEstados={() => setCurrentView('estados')}
        />
      ) : currentView === 'corridas' ? (
        <HistoricoCorridasPage
          activeView={currentView}
          onNavigateTelemetria={() => setCurrentView('telemetria')}
          onNavigateCorridas={() => setCurrentView('corridas')}
          onNavigateEstados={() => setCurrentView('estados')}
        />
      ) : (
        <EstadosPage
          activeView={currentView}
          onNavigateTelemetria={() => setCurrentView('telemetria')}
          onNavigateCorridas={() => setCurrentView('corridas')}
          onNavigateEstados={() => setCurrentView('estados')}
        />
      )}
    </main>
  );
}

export default App;