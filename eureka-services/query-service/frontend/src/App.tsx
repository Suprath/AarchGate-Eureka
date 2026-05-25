import { useState, useEffect } from 'react'

interface PipelineConfig {
  bootstrapServers: string;
  topic: string;
  groupId: string;
  batchSize: number;
  flushIntervalMs: number;
  databasePath: string;
  active: boolean;
}

interface SimulatorStatus {
  running: boolean;
  ratePerSecond: number;
  totalSimulated: number;
}

interface CachedFile {
  fileName: string;
  absolutePath: string;
  sizeBytes: number;
  lastModified: number;
}

interface QueryResult {
  totalMatches: number;
  bytesProcessed: number;
  executionTimeMs: number;
  speedGbSec: number;
  errorMessage: string;
}

export default function App() {
  // Query state
  const [queryText, setQueryText] = useState('status == 500 AND latency > 100')
  const [filePath, setFilePath] = useState('s3://eureka-logs/benchmark_native_scan.agb')
  const [pinMemory, setPinMemory] = useState(true)
  const [queryResult, setQueryResult] = useState<QueryResult | null>(null)
  const [querying, setQuerying] = useState(false)

  // Pipeline Config state
  const [config, setConfig] = useState<PipelineConfig>({
    bootstrapServers: '127.0.0.1:9092',
    topic: 'logs',
    groupId: 'aarchgate-eureka',
    batchSize: 1000,
    flushIntervalMs: 500,
    databasePath: 'live_stream.agb',
    active: false
  })
  const [savingConfig, setSavingConfig] = useState(false)

  // Simulator state
  const [simulator, setSimulator] = useState<SimulatorStatus>({
    running: false,
    ratePerSecond: 5000,
    totalSimulated: 0
  })
  const [togglingSimulator, setTogglingSimulator] = useState(false)

  // Cached files state
  const [cachedFiles, setCachedFiles] = useState<CachedFile[]>([])

  // Load configuration, simulator status, and cache files on mount
  useEffect(() => {
    fetchConfig()
    fetchSimulatorStatus()
    fetchCacheFiles()

    // Setup polling for live status and files
    const interval = setInterval(() => {
      fetchSimulatorStatus()
      fetchCacheFiles()
    }, 2000)

    return () => clearInterval(interval)
  }, [])

  const fetchConfig = async () => {
    try {
      const res = await fetch('/api/v1/pipeline/config')
      if (res.ok) {
        const data = await res.json()
        setConfig(data)
      }
    } catch (e) {
      console.error('Failed to fetch pipeline config', e)
    }
  }

  const fetchSimulatorStatus = async () => {
    try {
      const res = await fetch('/api/v1/pipeline/simulator/status')
      if (res.ok) {
        const data = await res.json()
        setSimulator(data)
      }
    } catch (e) {
      console.error('Failed to fetch simulator status', e)
    }
  }

  const fetchCacheFiles = async () => {
    try {
      const res = await fetch('/api/v1/pipeline/cache')
      if (res.ok) {
        const data = await res.json()
        setCachedFiles(data)
      }
    } catch (e) {
      console.error('Failed to fetch cache files', e)
    }
  }

  const handleSaveConfig = async (e: React.FormEvent) => {
    e.preventDefault()
    setSavingConfig(true)
    try {
      const res = await fetch('/api/v1/pipeline/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
      })
      if (res.ok) {
        const data = await res.json()
        setConfig(data)
        alert('Pipeline configuration updated successfully!')
      }
    } catch (err) {
      alert('Failed to update configuration')
    } finally {
      setSavingConfig(false)
    }
  }

  const handleToggleSimulator = async () => {
    setTogglingSimulator(true)
    const endpoint = simulator.running ? 'stop' : 'start'
    const url = `/api/v1/pipeline/simulator/${endpoint}?ratePerSecond=${simulator.ratePerSecond}&databasePath=${config.databasePath}`
    
    try {
      const res = await fetch(url, { method: 'POST' })
      if (res.ok) {
        await fetchSimulatorStatus()
      }
    } catch (err) {
      console.error('Simulator toggle failed', err)
    } finally {
      setTogglingSimulator(false)
    }
  }

  const handleRunQuery = async () => {
    setQuerying(true)
    setQueryResult(null)
    
    const params = new URLSearchParams()
    params.append('query', queryText)
    params.append('filePath', filePath)
    params.append('pinMemory', pinMemory.toString())

    try {
      const res = await fetch('/api/v1/queries/execute', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: params.toString()
      })
      
      const data = await res.json()
      setQueryResult(data)
    } catch (err) {
      setQueryResult({
        totalMatches: 0,
        bytesProcessed: 0,
        executionTimeMs: 0,
        speedGbSec: 0,
        errorMessage: 'Connection to Query service failed.'
      })
    } finally {
      setQuerying(false)
      fetchCacheFiles() // Refresh files list to show newly mapped file
    }
  }

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 Bytes'
    const k = 1024
    const sizes = ['Bytes', 'KB', 'MB', 'GB']
    const i = Math.floor(Math.log(bytes) / Math.log(k))
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
  }

  return (
    <div className="app-container">
      <header>
        <div className="brand">
          <span className="logo-badge">AG</span>
          <h1>AarchGate-Eureka Log Console</h1>
        </div>
        <div className="header-status">
          <div className="status-indicator">
            <span className="status-dot"></span>
            Spring Boot & C++ Daemon Connected
          </div>
        </div>
      </header>

      {/* Metrics Row */}
      <section className="stats-grid">
        <div className="card">
          <div className="card-title">Real-Time Ingestion</div>
          <div className="card-value">
            {simulator.running ? `${simulator.ratePerSecond.toLocaleString()} msg/s` : 'Inactive'}
          </div>
          <div className="card-subtext">
            {simulator.running ? 'Mock Queue Simulator Active' : 'Ingestion Pipeline Idle'}
          </div>
        </div>
        <div className="card">
          <div className="card-title">Total Logs Ingested</div>
          <div className="card-value">
            {simulator.totalSimulated.toLocaleString()}
          </div>
          <div className="card-subtext">Compacted in AGB format</div>
        </div>
        <div className="card">
          <div className="card-title">Cached Indices</div>
          <div className="card-value">
            {cachedFiles.length}
          </div>
          <div className="card-subtext">Mapped in Virtual Memory</div>
        </div>
        <div className="card">
          <div className="card-title">Aggregate Cache Size</div>
          <div className="card-value">
            {formatBytes(cachedFiles.reduce((acc, f) => acc + f.sizeBytes, 0))}
          </div>
          <div className="card-subtext">Pinned in Physical RAM</div>
        </div>
      </section>

      {/* Workspace Grid */}
      <main className="workspace-grid">
        
        {/* Left Column: JIT Query Terminal */}
        <section className="query-panel">
          <div className="panel">
            <div className="panel-header">
              <h2>⚡ JIT Logical Query Execution Terminal</h2>
            </div>
            
            <div className="input-group">
              <label>Log Data Target Path (Local Absolute or Cloud URI)</label>
              <input 
                type="text" 
                className="input-field" 
                value={filePath}
                onChange={e => setFilePath(e.target.value)}
                placeholder="e.g. s3://eureka-logs/benchmark_native_scan.agb"
              />
            </div>
            
            <div className="input-group" style={{ marginTop: '1rem' }}>
              <label>Bit-Sliced JIT Logic Query Predicate</label>
              <textarea 
                className="terminal-input"
                rows={3}
                value={queryText}
                onChange={e => setQueryText(e.target.value)}
                placeholder="e.g. status == 500 AND latency > 200"
              />
            </div>

            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <label className="checkbox-group">
                <input 
                  type="checkbox" 
                  checked={pinMemory} 
                  onChange={e => setPinMemory(e.target.checked)} 
                />
                Pin Mapped Memory Pages in RAM (mlock)
              </label>
              
              <button 
                className="btn" 
                onClick={handleRunQuery}
                disabled={querying}
              >
                {querying ? (
                  <>
                    <span className="status-dot spinning"></span>
                    Executing...
                  </>
                ) : (
                  'Run JIT Scan'
                )}
              </button>
            </div>

            {queryResult && (
              <div className="results-console">
                <div className="panel-header" style={{ marginBottom: '1rem', border: 'none' }}>
                  <h3>Scan Results</h3>
                </div>

                {queryResult.errorMessage ? (
                  <div style={{ color: 'var(--accent-red)', fontSize: '0.95rem' }}>
                    <strong>Error:</strong> {queryResult.errorMessage}
                  </div>
                ) : (
                  <>
                    <div className="metric-bar">
                      <div className="metric-item">
                        <div className="metric-lbl">Total Matches</div>
                        <div className="metric-val">{queryResult.totalMatches.toLocaleString()}</div>
                      </div>
                      <div className="metric-item">
                        <div className="metric-lbl">Bytes Processed</div>
                        <div className="metric-val">{formatBytes(queryResult.bytesProcessed)}</div>
                      </div>
                      <div className="metric-item">
                        <div className="metric-lbl">Scan Bandwidth</div>
                        <div className="metric-val speed">{queryResult.speedGbSec.toFixed(2)} GB/s</div>
                      </div>
                      <div className="metric-item">
                        <div className="metric-lbl">Execution Time</div>
                        <div className="metric-val time">{queryResult.executionTimeMs.toFixed(2)} ms</div>
                      </div>
                    </div>
                    <div style={{ color: 'var(--accent-green)', fontSize: '0.85rem', textAlign: 'center' }}>
                      JIT compiled bitslice filter completed successfully at hardware limits!
                    </div>
                  </>
                )}
              </div>
            )}
          </div>
        </section>

        {/* Right Column: Configuration & Cached Files */}
        <section className="side-panels">
          
          {/* Pipeline Config & Simulator Panel */}
          <div className="panel">
            <div className="panel-header">
              <h2>⚙️ Ingestion & Simulator Settings</h2>
            </div>
            
            <form onSubmit={handleSaveConfig}>
              <div className="form-grid">
                <div className="input-group">
                  <label>Kafka Brokers</label>
                  <input 
                    type="text" 
                    className="input-field" 
                    value={config.bootstrapServers}
                    onChange={e => setConfig({ ...config, bootstrapServers: e.target.value })}
                  />
                </div>
                <div className="input-group">
                  <label>Topic</label>
                  <input 
                    type="text" 
                    className="input-field" 
                    value={config.topic}
                    onChange={e => setConfig({ ...config, topic: e.target.value })}
                  />
                </div>
              </div>

              <div className="form-grid">
                <div className="input-group">
                  <label>Target databasePath</label>
                  <input 
                    type="text" 
                    className="input-field" 
                    value={config.databasePath}
                    onChange={e => setConfig({ ...config, databasePath: e.target.value })}
                  />
                </div>
                <div className="input-group">
                  <label>Pipeline State</label>
                  <label className="checkbox-group" style={{ height: '100%', alignItems: 'center' }}>
                    <input 
                      type="checkbox" 
                      checked={config.active}
                      onChange={e => setConfig({ ...config, active: e.target.checked })}
                    />
                    Kafka Consumer Active
                  </label>
                </div>
              </div>

              <div style={{ display: 'flex', gap: '1rem', marginTop: '1.25rem' }}>
                <button type="submit" className="btn btn-secondary" style={{ flex: 1 }} disabled={savingConfig}>
                  {savingConfig ? 'Saving...' : 'Apply Config'}
                </button>
              </div>
            </form>

            <hr style={{ border: 'none', borderBottom: '1px solid var(--border-color)', margin: '1.5rem 0' }} />

            <div>
              <h3>Mock Queue Simulator</h3>
              <p style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', margin: '-0.5rem 0 1rem' }}>
                Simulate continuous log generation to test live indexing speeds.
              </p>

              <div className="form-grid" style={{ marginBottom: '1.25rem' }}>
                <div className="input-group">
                  <label>Simulation Rate (msgs/sec)</label>
                  <input 
                    type="number" 
                    className="input-field" 
                    value={simulator.ratePerSecond}
                    onChange={e => setSimulator({ ...simulator, ratePerSecond: parseInt(e.target.value) || 1000 })}
                    disabled={simulator.running}
                  />
                </div>
                <div className="input-group" style={{ justifyContent: 'flex-end' }}>
                  <button 
                    onClick={handleToggleSimulator}
                    className={`btn ${simulator.running ? 'btn-danger' : ''}`}
                    style={{ width: '100%' }}
                    disabled={togglingSimulator}
                  >
                    {togglingSimulator ? 'Connecting...' : (simulator.running ? 'Stop Ingestion' : 'Start Ingestion')}
                  </button>
                </div>
              </div>
            </div>
          </div>

          {/* Cache Explorer Panel */}
          <div className="panel">
            <div className="panel-header">
              <h2>🗄️ Local Scratch Cache Directory</h2>
            </div>
            
            {cachedFiles.length === 0 ? (
              <div style={{ color: 'var(--text-secondary)', fontSize: '0.9rem', textAlign: 'center', padding: '1.5rem 0' }}>
                Scratch directory is empty. Run S3 queries or ingest logs to cache files.
              </div>
            ) : (
              <div className="file-list">
                {cachedFiles.map(file => (
                  <div className="file-item" key={file.fileName}>
                    <div className="file-info">
                      <span className="file-name">{file.fileName}</span>
                      <span className="file-meta">
                        Size: {formatBytes(file.sizeBytes)} | Staged: {new Date(file.lastModified).toLocaleTimeString()}
                      </span>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>

        </section>
      </main>
    </div>
  )
}
