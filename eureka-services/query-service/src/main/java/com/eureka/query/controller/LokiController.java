package com.eureka.query.controller;

import com.eureka.aarchgate.grpc.IngestResponse;
import com.eureka.aarchgate.grpc.QueryResponse;
import com.eureka.query.service.LogQLParser;
import com.eureka.query.service.QueryExecutionService;
import com.eureka.query.service.RealTimeLogBroadcaster;
import com.eureka.query.service.MockIngestionSimulator;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.io.IOException;
import java.util.*;

@RestController
@CrossOrigin(origins = "*")
public class LokiController {

    private final QueryExecutionService queryExecutionService;
    private final RealTimeLogBroadcaster broadcaster;
    private final MockIngestionSimulator simulator;
    private final ObjectMapper mapper = new ObjectMapper();

    @Value("${aarchgate.database-path}")
    private String databasePath;

    @Autowired
    public LokiController(QueryExecutionService queryExecutionService, RealTimeLogBroadcaster broadcaster, MockIngestionSimulator simulator) {
        this.queryExecutionService = queryExecutionService;
        this.broadcaster = broadcaster;
        this.simulator = simulator;
    }

    @GetMapping("/ready")
    public ResponseEntity<String> ready() {
        // Return 200 OK to indicate service readiness
        return ResponseEntity.ok("ready");
    }

    @GetMapping("/loki/api/v1/labels")
    public ResponseEntity<Map<String, Object>> getLabels() {
        Map<String, Object> response = new HashMap<>();
        response.put("status", "success");
        response.put("data", Arrays.asList("job", "level", "host", "status", "latency"));
        return ResponseEntity.ok(response);
    }

    @GetMapping("/loki/api/v1/label/{name}/values")
    public ResponseEntity<Map<String, Object>> getLabelValues(@PathVariable String name) {
        Map<String, Object> response = new HashMap<>();
        response.put("status", "success");

        List<String> values = new ArrayList<>();
        if ("job".equals(name)) {
            values = Arrays.asList("app-logs", "auth-service", "gateway-service");
        } else if ("level".equals(name)) {
            values = Arrays.asList("INFO", "WARNING", "ERROR", "DEBUG");
        } else if ("status".equals(name)) {
            values = Arrays.asList("200", "201", "400", "401", "404", "500");
        } else if ("host".equals(name)) {
            values = Arrays.asList("prod-web-node-0", "prod-web-node-1", "prod-auth-node-0");
        }

        response.put("data", values);
        return ResponseEntity.ok(response);
    }

    @PostMapping("/loki/api/v1/push")
    public ResponseEntity<Void> pushLogs(@RequestBody String requestBody) {
        try {
            @SuppressWarnings("unchecked")
            Map<String, Object> payload = mapper.readValue(requestBody, Map.class);
            List<String> logLinesToIngest = new ArrayList<>();

            @SuppressWarnings("unchecked")
            List<Map<String, Object>> streams = (List<Map<String, Object>>) payload.get("streams");
            if (streams != null) {
                for (Map<String, Object> streamObj : streams) {
                    @SuppressWarnings("unchecked")
                    Map<String, String> labels = (Map<String, String>) streamObj.get("stream");
                    @SuppressWarnings("unchecked")
                    List<List<String>> values = (List<List<String>>) streamObj.get("values");

                    if (values != null) {
                        for (List<String> valPair : values) {
                            if (valPair.size() >= 2) {
                                String rawLine = valPair.get(1);
                                
                                // Enrich JSON log line with labels if not present
                                if (rawLine.startsWith("{")) {
                                    try {
                                        @SuppressWarnings("unchecked")
                                        Map<String, Object> logMap = mapper.readValue(rawLine, Map.class);
                                        if (labels != null) {
                                            for (Map.Entry<String, String> entry : labels.entrySet()) {
                                                logMap.putIfAbsent(entry.getKey(), entry.getValue());
                                            }
                                        }
                                        rawLine = mapper.writeValueAsString(logMap);
                                    } catch (Exception ignored) {}
                                }
                                logLinesToIngest.add(rawLine);
                            }
                        }
                    }
                }
            }

            if (!logLinesToIngest.isEmpty()) {
                // 1. Dynamic WebSockets live tail push
                broadcaster.broadcastLogs(logLinesToIngest);

                // 2. Transcode and write index files via local C++ core gRPC Service
                IngestResponse ingestResponse = queryExecutionService.ingestLogLines(databasePath, logLinesToIngest);
                if (!ingestResponse.getSuccess()) {
                    System.err.println("[Loki HTTP Push] Transcoding failed: " + ingestResponse.getErrorMessage());
                    return ResponseEntity.status(HttpStatus.INTERNAL_SERVER_ERROR).build();
                }
            }

            return ResponseEntity.status(HttpStatus.NO_CONTENT).build(); // 204 No Content on success
        } catch (IOException e) {
            System.err.println("[Loki HTTP Push] Parsing failed: " + e.getMessage());
            return ResponseEntity.badRequest().build();
        }
    }

    @GetMapping("/loki/api/v1/query_range")
    public ResponseEntity<Map<String, Object>> queryRange(
            @RequestParam String query,
            @RequestParam(required = false, defaultValue = "100") int limit,
            @RequestParam(required = false) Long start,
            @RequestParam(required = false) Long end,
            @RequestParam(required = false, defaultValue = "backward") String direction) {

        // 1. Translate Loki query syntax (LogQL) to AarchGate format
        String translatedQuery = LogQLParser.parse(query);

        // 2. Query AarchGate C++ Engine via gRPC Client (pre-pin memory for execution)
        QueryResponse queryResponse = queryExecutionService.executeLogQuery(translatedQuery, databasePath, true);

        // 3. Assemble JSON payloads compliant with Loki format specifications
        Map<String, Object> response = new HashMap<>();
        response.put("status", "success");

        Map<String, Object> data = new HashMap<>();
        data.put("resultType", "streams");

        List<Map<String, Object>> matchedEvents = new ArrayList<>();
        try {
            matchedEvents = queryExecutionService.getMatchingEvents(translatedQuery, databasePath);
        } catch (Exception e) {
            System.err.println("[Loki Query Range] Failed to extract matching log events: " + e.getMessage());
        }

        List<Object[]> values = new ArrayList<>();
        for (Map<String, Object> event : matchedEvents) {
            if (values.size() >= limit) {
                break;
            }
            try {
                // Loki timestamps are Unix nanoseconds
                long nsTimestamp = System.currentTimeMillis() * 1_000_000L;
                Object tsObj = event.get("timestamp");
                if (tsObj instanceof Number) {
                    nsTimestamp = ((Number) tsObj).longValue() * 1_000_000L;
                }
                String lineContent = mapper.writeValueAsString(event);
                values.add(new Object[] { String.valueOf(nsTimestamp), lineContent });
            } catch (Exception ignored) {}
        }

        Map<String, Object> streamObj = new HashMap<>();
        Map<String, String> labels = new HashMap<>();
        labels.put("job", "aarchgate-query");
        streamObj.put("stream", labels);
        streamObj.put("values", values);

        data.put("result", Collections.singletonList(streamObj));
        response.put("data", data);

        return ResponseEntity.ok(response);
    }

    @PostMapping("/simulator/start")
    public ResponseEntity<String> startSimulator(
            @RequestParam(required = false, defaultValue = "100") int rate,
            @RequestParam(required = false) String path) {
        String targetPath = (path != null) ? path : databasePath;
        simulator.start(rate, targetPath);
        return ResponseEntity.ok("Simulator started at " + rate + " logs/sec writing to " + targetPath);
    }

    @PostMapping("/simulator/stop")
    public ResponseEntity<String> stopSimulator() {
        simulator.stop();
        return ResponseEntity.ok("Simulator stopped. Total simulated: " + simulator.getTotalSimulated());
    }

    @GetMapping("/simulator/status")
    public ResponseEntity<Map<String, Object>> getSimulatorStatus() {
        Map<String, Object> status = new HashMap<>();
        status.put("running", simulator.isRunning());
        status.put("ratePerSecond", simulator.getRatePerSecond());
        status.put("totalSimulated", simulator.getTotalSimulated());
        return ResponseEntity.ok(status);
    }
}
