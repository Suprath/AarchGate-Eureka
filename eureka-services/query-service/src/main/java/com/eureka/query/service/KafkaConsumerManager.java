package com.eureka.query.service;

import org.apache.kafka.clients.consumer.ConsumerConfig;
import org.apache.kafka.common.serialization.StringDeserializer;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Lazy;
import org.springframework.kafka.core.DefaultKafkaConsumerFactory;
import org.springframework.kafka.listener.ConcurrentMessageListenerContainer;
import org.springframework.kafka.listener.ContainerProperties;
import org.springframework.kafka.listener.MessageListener;
import org.springframework.stereotype.Service;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import java.util.*;
import java.util.concurrent.*;

@Service
public class KafkaConsumerManager {

    private final QueryExecutionService queryExecutionService;
    private final RealTimeLogBroadcaster broadcaster;

    @Value("${kafka.enabled:false}")
    private boolean kafkaEnabled;

    @Value("${kafka.bootstrap-servers:localhost:9092}")
    private String bootstrapServers;

    @Value("${kafka.topic:app-logs}")
    private String topic;

    @Value("${kafka.group-id:aarchgate-ingest}")
    private String groupId;

    @Value("${kafka.batch-size:5000}")
    private int batchSize;

    @Value("${kafka.flush-interval-ms:500}")
    private long flushIntervalMs;

    @Value("${aarchgate.database-path:scratch/logs.agb}")
    private String databasePath;

    private ConcurrentMessageListenerContainer<String, String> container;

    // Buffer for batch ingestion
    private final List<String> buffer = new CopyOnWriteArrayList<>();
    private final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();
    private ScheduledFuture<?> scheduledFlush;
    private long lastFlushTime = System.currentTimeMillis();

    @Autowired
    public KafkaConsumerManager(@Lazy QueryExecutionService queryExecutionService, RealTimeLogBroadcaster broadcaster) {
        this.queryExecutionService = queryExecutionService;
        this.broadcaster = broadcaster;
    }

    @PostConstruct
    public void init() {
        if (kafkaEnabled) {
            startContainer();
            // Start scheduled flushing task (checks flushIntervalMs)
            scheduledFlush = scheduler.scheduleWithFixedDelay(this::checkFlush, 100, 100, TimeUnit.MILLISECONDS);
        }
    }

    @PreDestroy
    public void cleanup() {
        stopContainer();
        if (scheduledFlush != null) {
            scheduledFlush.cancel(true);
        }
        scheduler.shutdown();
    }

    private synchronized void startContainer() {
        if (container != null && container.isRunning()) {
            return;
        }

        System.out.println("[Kafka Ingestion] Starting consumer container. Brokers: " + 
                bootstrapServers + ", Topic: " + topic);

        Map<String, Object> props = new HashMap<>();
        props.put(ConsumerConfig.BOOTSTRAP_SERVERS_CONFIG, bootstrapServers);
        props.put(ConsumerConfig.GROUP_ID_CONFIG, groupId);
        props.put(ConsumerConfig.KEY_DESERIALIZER_CLASS_CONFIG, StringDeserializer.class);
        props.put(ConsumerConfig.VALUE_DESERIALIZER_CLASS_CONFIG, StringDeserializer.class);
        props.put(ConsumerConfig.AUTO_OFFSET_RESET_CONFIG, "latest");
        props.put(ConsumerConfig.RECONNECT_BACKOFF_MS_CONFIG, 1000);
        props.put(ConsumerConfig.RECONNECT_BACKOFF_MAX_MS_CONFIG, 5000);

        DefaultKafkaConsumerFactory<String, String> cf = new DefaultKafkaConsumerFactory<>(props);
        ContainerProperties containerProps = new ContainerProperties(topic);

        containerProps.setMessageListener((MessageListener<String, String>) record -> {
            buffer.add(record.value());
            if (buffer.size() >= batchSize) {
                flushBuffer();
            }
        });

        container = new ConcurrentMessageListenerContainer<>(cf, containerProps);
        container.setAutoStartup(true);
        
        try {
            container.start();
        } catch (Exception e) {
            System.err.println("[Kafka Ingestion] Failed to start container: " + e.getMessage());
        }
    }

    private synchronized void stopContainer() {
        if (container != null && container.isRunning()) {
            System.out.println("[Kafka Ingestion] Stopping consumer container...");
            container.stop();
        }
        flushBuffer();
    }

    private void checkFlush() {
        if (buffer.isEmpty()) return;
        
        long timeSinceLastFlush = System.currentTimeMillis() - lastFlushTime;
        if (timeSinceLastFlush >= flushIntervalMs) {
            flushBuffer();
        }
    }

    private synchronized void flushBuffer() {
        if (buffer.isEmpty()) return;

        List<String> batchToFlush = new ArrayList<>(buffer);
        buffer.clear();
        lastFlushTime = System.currentTimeMillis();

        System.out.println("[Kafka Ingestion] Flushing batch of " + batchToFlush.size() + 
                " logs to database: " + databasePath);

        try {
            // 1. Broadcast to WebSockets live tail
            broadcaster.broadcastLogs(batchToFlush);

            // 2. Transcode and write index
            queryExecutionService.ingestLogLines(databasePath, batchToFlush);
        } catch (Exception e) {
            System.err.println("[Kafka Ingestion] Failed to transcode batch: " + e.getMessage());
        }
    }
}
