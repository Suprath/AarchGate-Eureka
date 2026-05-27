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

interface LogEvent {
  timestamp: number;
  level: string;
  status: number;
  latency: number;
  message: string;
  trace_id: string;
  host: string;
  source: string;
}

interface QueryResult {
  totalMatches: number;
  bytesProcessed: number;
  executionTimeMs: number;
  speedGbSec: number;
  errorMessage: string;
  events?: LogEvent[];
}

export default function App() {
  // Query state
  const [queryText, setQueryText] = useState('status == 500 AND latency > 100')
  const [filePath, setFilePath] = useState('s3://eureka-logs/benchmark_native_scan.agb')
  const [pinMemory, setPinMemory] = useState(true)
  const [queryResult, setQueryResult] = useState<QueryResult | null>(null)
  const [querying, setQuerying] = useState(false)
  const [activeTab, setActiveTab] = useState<'events' | 'visualization' | 'config'>('events')

  // Expanded events tracking
  const [expandedEvents, setExpandedEvents] = useState<Record<number, boolean>>({})

  // Field stats dialog tracking
  const [selectedFieldInfo, setSelectedFieldInfo] = useState<{ name: string; values: Record<string, number>; total: number } | null>(null)

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
    setExpandedEvents({})
    setSelectedFieldInfo(null)
    
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
      if (activeTab === 'config') {
        setActiveTab('events')
      }
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
      fetchCacheFiles()
    }
  }

  const formatBytes = (bytes: number) => {
    if (bytes === 0) return '0 Bytes'
    const k = 1024
    const sizes = ['Bytes', 'KB', 'MB', 'GB']
    const i = Math.floor(Math.log(bytes) / Math.log(k))
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
  }

  const toggleEventExpand = (index: number) => {
    setExpandedEvents(prev => ({
      ...prev,
      [index]: !prev[index]
    }))
  }

  // Inject condition helper into search bar
  const appendSearchCondition = (fieldName: string, value: any) => {
    const formatVal = typeof value === 'string' ? `"${value}"` : value
    const condition = `${fieldName} == ${formatVal}`
    if (queryText.trim() === '') {
      setQueryText(condition)
    } else if (queryText.toLowerCase().includes(fieldName.toLowerCase())) {
      // replace existing filter if present
      const regex = new RegExp(`${fieldName}\\s*(==|=|!=|>|>=|<|<=)\\s*[^\\s]+`, 'i')
      setQueryText(queryText.replace(regex, condition))
    } else {
      setQueryText(prev => `${prev} AND ${condition}`)
    }
  }

  // Calculate stats for left-panel interesting fields based on returned events
  const getFieldStats = (fieldName: keyof LogEvent) => {
    const events = queryResult?.events || []
    const counts: Record<string, number> = {}
    events.forEach(e => {
      const val = String(e[fieldName] ?? 'unknown')
      counts[val] = (counts[val] || 0) + 1
    })
    return counts
  }

  const handleFieldClick = (fieldName: keyof LogEvent) => {
    const stats = getFieldStats(fieldName)
    setSelectedFieldInfo({
      name: fieldName,
      values: stats,
      total: queryResult?.events?.length || 0
    })
  }

  const formatTime = (epoch: any) => {
    if (!epoch) return 'N/A';
    try {
      const date = new Date(epoch);
      if (isNaN(date.getTime())) return 'N/A';
      return date.toISOString().replace('T', ' ').substring(0, 19);
    } catch (e) {
      return 'N/A';
    }
  }

  // Helper to color-highlight raw NDJSON strings
  const renderHighlightEventJson = (log: LogEvent) => {
    const levelStr = String(log.level || 'INFO');
    const statusVal = log.status || 200;
    const latencyVal = log.latency || 0;
    const messageStr = log.message || '';
    
    return (
      <span className="json-line">
        {"{"}
        {log.timestamp !== undefined && log.timestamp !== null && (
          <><span className="json-key">"timestamp"</span>: <span className="json-num">{log.timestamp}</span>, </>
        )}
        <span className="json-key">"level"</span>: <span className={`json-val level-${levelStr.toLowerCase()}`}>"{levelStr}"</span>,{" "}
        <span className="json-key">"status"</span>: <span className={`json-num status-${String(statusVal)[0]}`}>{statusVal}</span>,{" "}
        <span className="json-key">"latency"</span>: <span className="json-num">{latencyVal}</span>,{" "}
        <span className="json-key">"message"</span>: <span className="json-val">"{messageStr}"</span>
        {log.trace_id !== undefined && log.trace_id !== null && (
          <>, <span className="json-key">"trace_id"</span>: <span className="json-val">"{log.trace_id}"</span></>
        )}
        {log.host !== undefined && log.host !== null && (
          <>, <span className="json-key">"host"</span>: <span className="json-val">"{log.host}"</span></>
        )}
        {log.source !== undefined && log.source !== null && (
          <>, <span className="json-key">"source"</span>: <span className="json-val">"{log.source}"</span></>
        )}
        {"}"}
      </span>
    )
  }

  // Timeline / Histogram data points based on timestamp distribution
  const renderHistogram = () => {
    const events = (queryResult?.events || []).filter(e => e.timestamp !== undefined && e.timestamp !== null);
    if (events.length === 0) {
      return (
        <div style={{ color: 'var(--text-secondary)', padding: '1.5rem', textAlign: 'center', background: 'var(--splunk-bg)', border: '1px solid var(--splunk-border)', borderRadius: '3px' }}>
          No timestamps found in matching results to visualize.
        </div>
      );
    }

    // Bin events by timestamp into 15 bars
    const times = events.map(e => e.timestamp).sort((a, b) => a - b);
    const minTime = times[0];
    const maxTime = times[times.length - 1];
    const range = Math.max(1, maxTime - minTime);
    
    const bins = Array(15).fill(0);
    events.forEach(e => {
      const index = Math.min(14, Math.floor(((e.timestamp - minTime) / range) * 15));
      bins[index]++;
    });

    const maxBinCount = Math.max(...bins, 1);

    return (
      <div className="histogram-container">
        <div className="histogram-bars">
          {bins.map((count, idx) => {
            const pct = (count / maxBinCount) * 100;
            return (
              <div className="histogram-col" key={idx} title={`${count} events`}>
                <div 
                  className="histogram-bar" 
                  style={{ height: `${Math.max(5, pct)}%`, background: pct > 0 ? 'var(--splunk-green)' : '#2a2f3a' }}
                />
                <span className="histogram-tick"></span>
              </div>
            );
          })}
        </div>
        <div className="histogram-labels">
          <span>{formatTime(minTime)}</span>
          <span>Timeline of matching log event frequencies</span>
          <span>{formatTime(maxTime)}</span>
        </div>
      </div>
    );
  }

  return (
    <div className="splunk-container">
      {/* Top Banner Navigation */}
      <header className="splunk-header">
        <div className="header-left">
          <span className="splunk-logo-brand">&gt; splunk &gt;</span>
          <span className="splunk-app-name">eureka_observability_console</span>
        </div>
        <div className="header-right">
          <div className={`connection-badge ${simulator.running ? 'running' : 'idle'}`}>
            <span className="pulse-dot"></span>
            {simulator.running ? 'DYNAMIC INGESTION RUNNING' : 'SYSTEM ONLINE | CONSOLE IDLE'}
          </div>
          <div className="header-stat-pill">
            DAEMON PORT: <span className="highlight">50052</span>
          </div>
        </div>
      </header>

      {/* Main Splunk Console Workspace */}
      <div className="console-layout">
        
        {/* Search & Controller Area */}
        <section className="search-section">
          <div className="search-bar-row">
            <span className="search-icon-badge">SPL</span>
            <input 
              type="text" 
              className="search-input" 
              value={queryText}
              onChange={e => setQueryText(e.target.value)}
              placeholder="e.g. status == 500 AND latency > 100"
            />
            
            <select className="time-picker">
              <option>All time (real-time)</option>
              <option>Last 15 minutes</option>
              <option>Last hour</option>
              <option>Last 24 hours</option>
            </select>

            <button 
              className={`search-btn ${querying ? 'loading' : ''}`}
              onClick={handleRunQuery}
              disabled={querying}
            >
              {querying ? 'Scanning...' : 'Search'}
            </button>
          </div>

          <div className="search-options-row">
            <div className="option-item">
              <label>Log Store Target:</label>
              <input 
                type="text" 
                className="target-path-input"
                value={filePath}
                onChange={e => setFilePath(e.target.value)}
                placeholder="e.g. s3://eureka-logs/benchmark_native_scan.agb"
              />
            </div>
            <div className="option-item checkbox">
              <label className="checkbox-label">
                <input 
                  type="checkbox" 
                  checked={pinMemory} 
                  onChange={e => setPinMemory(e.target.checked)} 
                />
                Pin mapped virtual memory (mlock)
              </label>
            </div>
          </div>
        </section>

        {/* Tab Selection Row */}
        <div className="tabs-header">
          <div className="tabs-left">
            <button 
              className={`tab-btn ${activeTab === 'events' ? 'active' : ''}`}
              onClick={() => setActiveTab('events')}
            >
              Events ({queryResult?.events?.length || 0})
            </button>
            <button 
              className={`tab-btn ${activeTab === 'visualization' ? 'active' : ''}`}
              onClick={() => setActiveTab('visualization')}
              disabled={!queryResult}
            >
              Visualization
            </button>
            <button 
              className={`tab-btn ${activeTab === 'config' ? 'active' : ''}`}
              onClick={() => setActiveTab('config')}
            >
              Kafka Ingestion & Settings
            </button>
          </div>

          {queryResult && (
            <div className="tabs-right-stats">
              <span>Matches: <strong className="stat-highlight">{queryResult.totalMatches.toLocaleString()}</strong></span>
              <span className="divider">|</span>
              <span>Latency: <strong className="stat-highlight text-green">{queryResult.executionTimeMs.toFixed(2)} ms</strong></span>
              <span className="divider">|</span>
              <span>Speed: <strong className="stat-highlight text-cyan">{queryResult.speedGbSec.toFixed(2)} GB/s</strong></span>
              <span className="divider">|</span>
              <span>Processed: <strong className="stat-highlight">{formatBytes(queryResult.bytesProcessed)}</strong></span>
            </div>
          )}
        </div>

        {/* Dynamic Display Panels */}
        {activeTab === 'events' && (
          <div className="workspace-panels">
            {/* Sidebar: Fields Panel */}
            <aside className="fields-sidebar">
              <div className="sidebar-group">
                <h3>Selected Fields</h3>
                <ul className="fields-list">
                  <li onClick={() => handleFieldClick('host')}>
                    <span className="field-name">host</span>
                    <span className="field-count">5</span>
                  </li>
                  <li onClick={() => handleFieldClick('source')}>
                    <span className="field-name">source</span>
                    <span className="field-count">2</span>
                  </li>
                </ul>
              </div>

              <div className="sidebar-group" style={{ marginTop: '1.5rem' }}>
                <h3>Interesting Fields</h3>
                <ul className="fields-list">
                  <li onClick={() => handleFieldClick('level')}>
                    <span className="field-name">level</span>
                    <span className="field-count">4</span>
                  </li>
                  <li onClick={() => handleFieldClick('status')}>
                    <span className="field-name">status</span>
                    <span className="field-count">6</span>
                  </li>
                  <li onClick={() => handleFieldClick('latency')}>
                    <span className="field-name">latency</span>
                    <span className="field-count">#</span>
                  </li>
                  <li onClick={() => handleFieldClick('trace_id')}>
                    <span className="field-name">trace_id</span>
                    <span className="field-count">a</span>
                  </li>
                </ul>
              </div>

              {/* Cache explorer nested in sidebar */}
              <div className="sidebar-cache-box">
                <h3>Scratch Cache Files</h3>
                <div className="sidebar-cache-list">
                  {cachedFiles.map(file => (
                    <div className="cache-file-pill" key={file.fileName} title={file.absolutePath}>
                      <span className="pill-name">{file.fileName.length > 25 ? '...' + file.fileName.substring(file.fileName.length - 25) : file.fileName}</span>
                      <span className="pill-size">{formatBytes(file.sizeBytes)}</span>
                    </div>
                  ))}
                  {cachedFiles.length === 0 && <span className="empty-text">No cached indices.</span>}
                </div>
              </div>
            </aside>

            {/* Main Center Events View */}
            <main className="events-main">
              {/* Field quick dialog overlay */}
              {selectedFieldInfo && (
                <div className="field-stats-modal">
                  <div className="modal-header">
                    <h4>Field Stats: <span className="modal-field-name">{selectedFieldInfo.name}</span></h4>
                    <button className="close-btn" onClick={() => setSelectedFieldInfo(null)}>&times;</button>
                  </div>
                  <div className="modal-body">
                    <p className="summary-text">Values distribution in matching records ({selectedFieldInfo.total} events):</p>
                    <div className="stats-bars-list">
                      {Object.entries(selectedFieldInfo.values).map(([val, count]) => {
                        const pct = selectedFieldInfo.total > 0 ? (count / selectedFieldInfo.total) * 100 : 0
                        return (
                          <div className="stats-bar-row" key={val} onClick={() => {
                            appendSearchCondition(selectedFieldInfo.name, val)
                            setSelectedFieldInfo(null)
                          }}>
                            <div className="bar-label">
                              <span className="bar-val-text">{val}</span>
                              <span className="bar-count-text">{count} ({pct.toFixed(1)}%)</span>
                            </div>
                            <div className="bar-outer">
                              <div className="bar-inner" style={{ width: `${pct}%` }}></div>
                            </div>
                          </div>
                        )
                      })}
                    </div>
                  </div>
                </div>
              )}

              {/* Event results list */}
              <div className="events-list-container">
                {queryResult?.errorMessage && (
                  <div className="search-error-banner">
                    <strong>Search Error:</strong> {queryResult.errorMessage}
                  </div>
                )}

                {queryResult && queryResult.events && queryResult.events.length > 0 ? (
                  <div className="events-table-view">
                    <div className="events-table-header">
                      <div className="th-col index">#</div>
                      <div className="th-col time">Time</div>
                      <div className="th-col event">Event</div>
                    </div>

                    <div className="events-table-body">
                      {queryResult.events.map((log, idx) => {
                        const isExpanded = !!expandedEvents[idx]
                        return (
                          <div className={`event-row-wrapper ${isExpanded ? 'expanded' : ''}`} key={idx}>
                            <div className="event-row-summary" onClick={() => toggleEventExpand(idx)}>
                              <div className="td-col index">
                                <span className={`expand-chevron ${isExpanded ? 'expanded' : ''}`}>&#9654;</span>
                                {idx + 1}
                              </div>
                              <div className="td-col time">{formatTime(log.timestamp)}</div>
                              <div className="td-col event-raw">
                                {renderHighlightEventJson(log)}
                              </div>
                            </div>

                            {/* Event details drawer */}
                            {isExpanded && (
                              <div className="event-expanded-details">
                                <table className="parsed-fields-table">
                                  <thead>
                                    <tr>
                                      <th>Field</th>
                                      <th>Value</th>
                                      <th>Actions</th>
                                    </tr>
                                  </thead>
                                  <tbody>
                                    {Object.entries(log).map(([key, value]) => (
                                      <tr key={key}>
                                        <td className="field-key">{key}</td>
                                        <td className="field-value">{String(value)}</td>
                                        <td>
                                          <button 
                                            className="action-link-btn" 
                                            onClick={() => appendSearchCondition(key, value)}
                                            title="Add this filter to search query"
                                          >
                                            Add to search
                                          </button>
                                        </td>
                                      </tr>
                                    ))}
                                  </tbody>
                                </table>
                              </div>
                            )}
                          </div>
                        )
                      })}
                    </div>
                  </div>
                ) : (
                  <div className="empty-results-banner">
                    {!queryResult ? 'Write a query and click Search to scan binary logs.' : 'No log lines match the filter predicates.'}
                  </div>
                )}
              </div>
            </main>
          </div>
        )}

        {activeTab === 'visualization' && (
          <div className="panel splunk-panel">
            <h2>📊 JIT Compiled Search Event Density Over Time</h2>
            <p style={{ color: 'var(--text-secondary)', fontSize: '0.85rem', marginBottom: '1.5rem' }}>
              Splunk-like chronological visualization of filtered log hits parsed dynamically from memory.
            </p>
            {renderHistogram()}
          </div>
        )}

        {activeTab === 'config' && (
          <div className="config-tab-panels">
            {/* Kafka Config Panel */}
            <div className="panel splunk-panel">
              <div className="panel-header">
                <h2>⚙️ Kafka Pipeline Configuration Settings</h2>
              </div>
              <form onSubmit={handleSaveConfig}>
                <div className="form-grid">
                  <div className="input-group">
                    <label>Bootstrap Brokers</label>
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
                    <label>Consumer Group ID</label>
                    <input 
                      type="text" 
                      className="input-field" 
                      value={config.groupId}
                      onChange={e => setConfig({ ...config, groupId: e.target.value })}
                    />
                  </div>
                  <div className="input-group">
                    <label>Flush Batch Size</label>
                    <input 
                      type="number" 
                      className="input-field" 
                      value={config.batchSize}
                      onChange={e => setConfig({ ...config, batchSize: parseInt(e.target.value) || 1000 })}
                    />
                  </div>
                </div>

                <div className="form-grid">
                  <div className="input-group">
                    <label>Compile Flush Interval (ms)</label>
                    <input 
                      type="number" 
                      className="input-field" 
                      value={config.flushIntervalMs}
                      onChange={e => setConfig({ ...config, flushIntervalMs: parseInt(e.target.value) || 500 })}
                    />
                  </div>
                  <div className="input-group">
                    <label>Target Compact Database File (.agb)</label>
                    <input 
                      type="text" 
                      className="input-field" 
                      value={config.databasePath}
                      onChange={e => setConfig({ ...config, databasePath: e.target.value })}
                    />
                  </div>
                </div>

                <div className="form-grid" style={{ alignItems: 'center', marginTop: '0.5rem' }}>
                  <label className="checkbox-group">
                    <input 
                      type="checkbox" 
                      checked={config.active}
                      onChange={e => setConfig({ ...config, active: e.target.checked })}
                    />
                    Kafka Ingestion Consumer Active
                  </label>

                  <button type="submit" className="btn btn-secondary" style={{ width: '100%' }} disabled={savingConfig}>
                    {savingConfig ? 'Saving Configurations...' : 'Apply & Restart Consumer'}
                  </button>
                </div>
              </form>
            </div>

            {/* Ingestion Simulator Panel */}
            <div className="panel splunk-panel">
              <div className="panel-header">
                <h2>⚡ High-Throughput Log Stream Simulator</h2>
              </div>
              <p style={{ color: 'var(--text-secondary)', fontSize: '0.85rem', marginTop: '-0.75rem', marginBottom: '1.5rem' }}>
                Simulate an active message queue stream directly feeding raw log payloads to your ingestion pipeline.
              </p>

              <div className="form-grid" style={{ alignItems: 'center' }}>
                <div className="input-group">
                  <label>Simulation Rate (Messages / Sec)</label>
                  <input 
                    type="number" 
                    className="input-field" 
                    value={simulator.ratePerSecond}
                    onChange={e => setSimulator({ ...simulator, ratePerSecond: parseInt(e.target.value) || 5000 })}
                    disabled={simulator.running}
                  />
                </div>
                <div className="input-group">
                  <button 
                    onClick={handleToggleSimulator}
                    className={`btn ${simulator.running ? 'btn-danger' : 'btn-success-splunk'}`}
                    disabled={togglingSimulator}
                    style={{ height: '2.5rem', marginTop: '1.3rem' }}
                  >
                    {togglingSimulator ? 'Connecting...' : (simulator.running ? 'Stop Log Generation' : 'Start Log Generation')}
                  </button>
                </div>
              </div>

              <div className="simulator-stats-display" style={{ marginTop: '1.5rem' }}>
                <div className="sim-stat-box">
                  <span className="lbl">SIMULATION STATE</span>
                  <span className={`val ${simulator.running ? 'text-green' : 'text-red'}`}>
                    {simulator.running ? 'ACTIVE INGESTION' : 'INACTIVE'}
                  </span>
                </div>
                <div className="sim-stat-box">
                  <span className="lbl">TOTAL SIMULATED EVENTS</span>
                  <span className="val">{simulator.totalSimulated.toLocaleString()}</span>
                </div>
              </div>
            </div>
          </div>
        )}

      </div>
    </div>
  )
}
