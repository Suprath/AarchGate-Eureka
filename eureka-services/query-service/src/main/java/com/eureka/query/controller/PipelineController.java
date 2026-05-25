package com.eureka.query.controller;

import com.eureka.query.model.PipelineConfig;
import com.eureka.query.service.KafkaConsumerManager;
import com.eureka.query.service.MockIngestionSimulator;
import com.eureka.query.service.ScratchStorageService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/v1/pipeline")
@CrossOrigin(origins = "*")
public class PipelineController {

    private final KafkaConsumerManager kafkaConsumerManager;
    private final MockIngestionSimulator mockSimulator;
    private final ScratchStorageService scratchStorageService;

    @Autowired
    public PipelineController(KafkaConsumerManager kafkaConsumerManager,
                              MockIngestionSimulator mockSimulator,
                              ScratchStorageService scratchStorageService) {
        this.kafkaConsumerManager = kafkaConsumerManager;
        this.mockSimulator = mockSimulator;
        this.scratchStorageService = scratchStorageService;
    }

    @GetMapping("/config")
    public ResponseEntity<PipelineConfig> getConfig() {
        return ResponseEntity.ok(kafkaConsumerManager.getActiveConfig());
    }

    @PostMapping("/config")
    public ResponseEntity<PipelineConfig> updateConfig(@RequestBody PipelineConfig newConfig) {
        kafkaConsumerManager.updateConfig(newConfig);
        return ResponseEntity.ok(kafkaConsumerManager.getActiveConfig());
    }

    @PostMapping("/simulator/start")
    public ResponseEntity<Map<String, Object>> startSimulator(
            @RequestParam(required = false, defaultValue = "5000") int ratePerSecond,
            @RequestParam(required = false, defaultValue = "live_stream.agb") String databasePath) {
        
        mockSimulator.start(ratePerSecond, databasePath);
        
        Map<String, Object> response = new HashMap<>();
        response.put("success", true);
        response.put("message", "Mock Log Ingestion Simulator started at rate: " + ratePerSecond + " logs/sec");
        return ResponseEntity.ok(response);
    }

    @PostMapping("/simulator/stop")
    public ResponseEntity<Map<String, Object>> stopSimulator() {
        mockSimulator.stop();
        Map<String, Object> response = new HashMap<>();
        response.put("success", true);
        response.put("message", "Mock Log Ingestion Simulator stopped");
        return ResponseEntity.ok(response);
    }

    @GetMapping("/simulator/status")
    public ResponseEntity<Map<String, Object>> getSimulatorStatus() {
        Map<String, Object> response = new HashMap<>();
        response.put("running", mockSimulator.isRunning());
        response.put("ratePerSecond", mockSimulator.getRatePerSecond());
        response.put("totalSimulated", mockSimulator.getTotalSimulated());
        return ResponseEntity.ok(response);
    }

    @GetMapping("/cache")
    public ResponseEntity<List<Map<String, Object>>> getCachedFiles() {
        return ResponseEntity.ok(scratchStorageService.getCachedFiles());
    }
}
