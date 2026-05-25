package com.eureka.query.service;

import com.eureka.query.model.PipelineConfig;
import com.eureka.query.repository.PipelineConfigRepository;
import org.apache.kafka.clients.consumer.ConsumerConfig;
import org.apache.kafka.common.serialization.StringDeserializer;
import org.springframework.beans.factory.annotation.Autowired;
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

    private final PipelineConfigRepository repository;
    private final QueryExecutionService queryExecutionService;

    private ConcurrentMessageListenerContainer<String, String> container;
    private PipelineConfig activeConfig;

    // Buffer for batch ingestion
    private final List<String> buffer = new CopyOnWriteArrayList<>();
    private final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();
    private ScheduledFuture<?> scheduledFlush;
    private long lastFlushTime = System.currentTimeMillis();

    @Autowired
    public KafkaConsumerManager(PipelineConfigRepository repository, 
                                @Lazy QueryExecutionService queryExecutionService) {
        this.repository = repository;
        this.queryExecutionService = queryExecutionService;
    }

    @PostConstruct
    public void init() {
        // Load config from DB. If empty, create default
        List<PipelineConfig> configs = repository.findAll();
        if (configs.isEmpty()) {
            activeConfig = new PipelineConfig();
            repository.save(activeConfig);
        } else {
            activeConfig = configs.get(0);
        }

        // Apply config
        if (activeConfig.isActive()) {
            startContainer();
        }

        // Start scheduled flushing task (checks flushIntervalMs)
        scheduledFlush = scheduler.scheduleWithFixedDelay(this::checkFlush, 100, 100, TimeUnit.MILLISECONDS);
    }

    @PreDestroy
    public void cleanup() {
        stopContainer();
        if (scheduledFlush != null) {
            scheduledFlush.cancel(true);
        }
        scheduler.shutdown();
    }

    public synchronized void updateConfig(PipelineConfig newConfig) {
        // Save to DB
        activeConfig.setBootstrapServers(newConfig.getBootstrapServers());
        activeConfig.setTopic(newConfig.getTopic());
        activeConfig.setGroupId(newConfig.getGroupId());
        activeConfig.setBatchSize(newConfig.getBatchSize());
        activeConfig.setFlushIntervalMs(newConfig.getFlushIntervalMs());
        activeConfig.setDatabasePath(newConfig.getDatabasePath());
        activeConfig.setActive(newConfig.isActive());
        repository.save(activeConfig);

        // Restart with new config
        stopContainer();
        if (activeConfig.isActive()) {
            startContainer();
        }
    }

    public PipelineConfig getActiveConfig() {
        return activeConfig;
    }

    private synchronized void startContainer() {
        if (container != null && container.isRunning()) {
            return;
        }

        System.out.println("[Kafka Ingestion] Starting consumer container. Brokers: " + 
                activeConfig.getBootstrapServers() + ", Topic: " + activeConfig.getTopic());

        Map<String, Object> props = new HashMap<>();
        props.put(ConsumerConfig.BOOTSTRAP_SERVERS_CONFIG, activeConfig.getBootstrapServers());
        props.put(ConsumerConfig.GROUP_ID_CONFIG, activeConfig.getGroupId());
        props.put(ConsumerConfig.KEY_DESERIALIZER_CLASS_CONFIG, StringDeserializer.class);
        props.put(ConsumerConfig.VALUE_DESERIALIZER_CLASS_CONFIG, StringDeserializer.class);
        props.put(ConsumerConfig.AUTO_OFFSET_RESET_CONFIG, "latest");
        props.put(ConsumerConfig.RECONNECT_BACKOFF_MS_CONFIG, 1000);
        props.put(ConsumerConfig.RECONNECT_BACKOFF_MAX_MS_CONFIG, 5000);

        DefaultKafkaConsumerFactory<String, String> cf = new DefaultKafkaConsumerFactory<>(props);
        ContainerProperties containerProps = new ContainerProperties(activeConfig.getTopic());

        containerProps.setMessageListener((MessageListener<String, String>) record -> {
            buffer.add(record.value());
            if (buffer.size() >= activeConfig.getBatchSize()) {
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
        if (timeSinceLastFlush >= activeConfig.getFlushIntervalMs()) {
            flushBuffer();
        }
    }

    private synchronized void flushBuffer() {
        if (buffer.isEmpty()) return;

        List<String> batchToFlush = new ArrayList<>(buffer);
        buffer.clear();
        lastFlushTime = System.currentTimeMillis();

        System.out.println("[Kafka Ingestion] Flushing batch of " + batchToFlush.size() + 
                " logs to database: " + activeConfig.getDatabasePath());

        try {
            queryExecutionService.ingestLogLines(activeConfig.getDatabasePath(), batchToFlush);
        } catch (Exception e) {
            System.err.println("[Kafka Ingestion] Failed to transcode batch: " + e.getMessage());
        }
    }
}
