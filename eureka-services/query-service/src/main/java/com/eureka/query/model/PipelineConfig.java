package com.eureka.query.model;

import jakarta.persistence.*;

@Entity
@Table(name = "pipeline_config")
public class PipelineConfig {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false)
    private String bootstrapServers = "localhost:9092";

    @Column(nullable = false)
    private String topic = "logs";

    @Column(nullable = false)
    private String groupId = "aarchgate-eureka";

    @Column(nullable = false)
    private int batchSize = 1000;

    @Column(nullable = false)
    private int flushIntervalMs = 500;

    @Column(nullable = false)
    private String databasePath = "live_stream.agb";

    @Column(nullable = false)
    private boolean active = false;

    // Getters and Setters
    public Long getId() {
        return id;
    }

    public void setId(Long id) {
        this.id = id;
    }

    public String getBootstrapServers() {
        return bootstrapServers;
    }

    public void setBootstrapServers(String bootstrapServers) {
        this.bootstrapServers = bootstrapServers;
    }

    public String getTopic() {
        return topic;
    }

    public void setTopic(String topic) {
        this.topic = topic;
    }

    public String getGroupId() {
        return groupId;
    }

    public void setGroupId(String groupId) {
        this.groupId = groupId;
    }

    public int getBatchSize() {
        return batchSize;
    }

    public void setBatchSize(int batchSize) {
        this.batchSize = batchSize;
    }

    public int getFlushIntervalMs() {
        return flushIntervalMs;
    }

    public void setFlushIntervalMs(int flushIntervalMs) {
        this.flushIntervalMs = flushIntervalMs;
    }

    public String getDatabasePath() {
        return databasePath;
    }

    public void setDatabasePath(String databasePath) {
        this.databasePath = databasePath;
    }

    public boolean isActive() {
        return active;
    }

    public void setActive(boolean active) {
        this.active = active;
    }
}
