package com.eureka.query.service;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Lazy;
import org.springframework.stereotype.Service;

import jakarta.annotation.PreDestroy;
import java.util.*;
import java.util.concurrent.*;

@Service
public class MockIngestionSimulator {

    private final QueryExecutionService queryExecutionService;
    private final ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();
    private ScheduledFuture<?> task;

    private boolean running = false;
    private int ratePerSecond = 5000;
    private String databasePath = "live_stream.agb";
    
    private final RealTimeLogBroadcaster broadcaster;
    private final Random random = new Random();
    private long totalSimulated = 0;

    @Autowired
    public MockIngestionSimulator(@Lazy QueryExecutionService queryExecutionService, RealTimeLogBroadcaster broadcaster) {
        this.queryExecutionService = queryExecutionService;
        this.broadcaster = broadcaster;
    }

    public synchronized void start(int ratePerSecond, String databasePath) {
        if (running) {
            stop();
        }

        this.ratePerSecond = ratePerSecond;
        this.databasePath = databasePath;
        this.running = true;

        System.out.println("[Mock Ingestion] Starting simulator. Rate: " + ratePerSecond + 
                " logs/sec, Database: " + databasePath);

        // Schedule task to generate logs every 200ms
        int delayMs = 200;
        int batchSize = (ratePerSecond * delayMs) / 1000;
        if (batchSize < 1) batchSize = 1;

        final int finalBatchSize = batchSize;
        task = executor.scheduleAtFixedRate(() -> generateAndIngest(finalBatchSize), 
                delayMs, delayMs, TimeUnit.MILLISECONDS);
    }

    public synchronized void stop() {
        if (!running) return;
        
        System.out.println("[Mock Ingestion] Stopping simulator...");
        if (task != null) {
            task.cancel(true);
        }
        running = false;
    }

    public boolean isRunning() {
        return running;
    }

    public int getRatePerSecond() {
        return ratePerSecond;
    }

    public long getTotalSimulated() {
        return totalSimulated;
    }

    @PreDestroy
    public void cleanup() {
        stop();
        executor.shutdown();
    }

    private void generateAndIngest(int batchSize) {
        List<String> logs = new ArrayList<>(batchSize);
        for (int i = 0; i < batchSize; i++) {
            logs.add(generateMockLog());
        }

        try {
            // 1. Send live stream updates to WebSocket subscribers
            broadcaster.broadcastLogs(logs);

            // 2. Transcode and write to database core
            queryExecutionService.ingestLogLines(databasePath, logs);
            totalSimulated += batchSize;
        } catch (Exception e) {
            System.err.println("[Mock Ingestion] Simulator error: " + e.getMessage());
        }
    }

    private String generateMockLog() {
        // Status codes: 200 (80%), 404 (10%), 500 (10%)
        int rand = random.nextInt(100);
        int status = 200;
        if (rand >= 80 && rand < 90) status = 404;
        else if (rand >= 90) status = 500;

        // Latency: 200 matches average latency, but some high peaks
        int latency = random.nextInt(80) + 10; // 10 to 90 ms
        if (status == 500) {
            latency = random.nextInt(400) + 150; // 150 to 550 ms
        }

        // Severity: 1 (INFO), 2 (WARN), 3 (ERROR)
        int severity = 1;
        if (status == 404) severity = 2;
        if (status == 500) severity = 3;

        return String.format("{\"status\":%d,\"latency\":%d,\"severity\":%d,\"timestamp\":%d}", 
                status, latency, severity, System.currentTimeMillis());
    }
}
