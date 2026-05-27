package com.eureka.query.controller;

import com.eureka.aarchgate.grpc.QueryResponse;
import com.eureka.aarchgate.grpc.IngestResponse;
import com.eureka.query.service.QueryExecutionService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/v1/queries")
public class QueryController {

    private final QueryExecutionService queryExecutionService;

    @Autowired
    public QueryController(QueryExecutionService queryExecutionService) {
        this.queryExecutionService = queryExecutionService;
    }

    @PostMapping("/execute")
    public ResponseEntity<Map<String, Object>> executeQuery(
            @RequestParam String query,
            @RequestParam String filePath,
            @RequestParam(required = false, defaultValue = "false") boolean pinMemory) {
        
        QueryResponse response = queryExecutionService.executeLogQuery(query, filePath, pinMemory);

        Map<String, Object> result = new HashMap<>();
        result.put("totalMatches", response.getTotalMatches());
        result.put("bytesProcessed", response.getBytesProcessed());
        result.put("executionTimeMs", response.getExecutionTimeMs());
        result.put("speedGbSec", response.getSpeedGbSec());
        result.put("errorMessage", response.getErrorMessage());

        if (response.getErrorMessage() != null && !response.getErrorMessage().isEmpty()) {
            return ResponseEntity.badRequest().body(result);
        }

        // Fetch matching event text lines to serve the events viewer
        try {
            List<Map<String, Object>> events = queryExecutionService.getMatchingEvents(query, filePath);
            result.put("events", events);
        } catch (Exception e) {
            result.put("events", new ArrayList<>());
            System.err.println("[QueryController] Failed to extract matching log events: " + e.getMessage());
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/ingest")
    public ResponseEntity<Map<String, Object>> ingestLogs(
            @RequestParam String databasePath,
            @RequestBody List<String> logLines) {

        IngestResponse response = queryExecutionService.ingestLogLines(databasePath, logLines);

        Map<String, Object> result = new HashMap<>();
        result.put("totalIngested", response.getTotalIngested());
        result.put("success", response.getSuccess());
        result.put("errorMessage", response.getErrorMessage());

        if (!response.getSuccess()) {
            return ResponseEntity.badRequest().body(result);
        }
        return ResponseEntity.ok(result);
    }
}
