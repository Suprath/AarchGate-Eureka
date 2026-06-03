package com.eureka.query.service;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.stereotype.Service;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@Service
public class RealTimeLogBroadcaster {

    private final Map<WebSocketSession, TailSubscription> subscriptions = new ConcurrentHashMap<>();
    private final ObjectMapper mapper = new ObjectMapper();

    public static class TailSubscription {
        public final String originalQuery;
        public final String parsedQuery; // translated to status == X AND latency > Y etc.
        public Integer statusTarget;
        public Integer latencyTarget;
        public String lineFilter;

        public TailSubscription(String query) {
            this.originalQuery = query;
            this.parsedQuery = LogQLParser.parse(query);

            // Parse status and latency targets if they exist in translated predicate
            Pattern statusPat = Pattern.compile("status\\s*==\\s*(\\d+)");
            Matcher statusMat = statusPat.matcher(parsedQuery);
            if (statusMat.find()) {
                this.statusTarget = Integer.parseInt(statusMat.group(1));
            }

            Pattern latencyPat = Pattern.compile("latency\\s*>\\s*(\\d+)");
            Matcher latencyMat = latencyPat.matcher(parsedQuery);
            if (latencyMat.find()) {
                this.latencyTarget = Integer.parseInt(latencyMat.group(1));
            }

            // Extract line filter if present (e.g. from original query like |= "match")
            Pattern filterPat = Pattern.compile("\\|=\\s*\"([^\"]*)\"");
            Matcher filterMat = filterPat.matcher(query);
            if (filterMat.find()) {
                this.lineFilter = filterMat.group(1);
            } else if (statusTarget == null && latencyTarget == null && !parsedQuery.isEmpty()) {
                this.lineFilter = parsedQuery;
            }
        }

        public boolean matches(Map<String, Object> log, String rawLine) {
            // Check status
            if (statusTarget != null) {
                Object statusObj = log.get("status");
                if (statusObj == null) return false;
                try {
                    int statusVal = ((Number) statusObj).intValue();
                    if (statusVal != statusTarget) return false;
                } catch (Exception e) {
                    return false;
                }
            }

            // Check latency
            if (latencyTarget != null) {
                Object latencyObj = log.get("latency");
                if (latencyObj == null) return false;
                try {
                    int latencyVal = ((Number) latencyObj).intValue();
                    if (latencyVal <= latencyTarget) return false;
                } catch (Exception e) {
                    return false;
                }
            }

            // Check line filter
            if (lineFilter != null && !lineFilter.isEmpty()) {
                Object msgObj = log.get("message");
                String msgStr = (msgObj != null) ? msgObj.toString() : rawLine;
                if (!msgStr.contains(lineFilter)) return false;
            }

            return true;
        }
    }

    public void registerSession(WebSocketSession session, String query) {
        subscriptions.put(session, new TailSubscription(query));
        System.out.println("[WebSockets] Session registered: " + session.getId() + " for query: " + query);
    }

    public void deregisterSession(WebSocketSession session) {
        subscriptions.remove(session);
        System.out.println("[WebSockets] Session deregistered: " + session.getId());
    }

    public Map<WebSocketSession, TailSubscription> getSubscriptions() {
        return subscriptions;
    }

    public void broadcastLogs(List<String> rawLogLines) {
        if (subscriptions.isEmpty() || rawLogLines.isEmpty()) {
            return;
        }

        // Pre-parse the batch of logs into Map representations for quick evaluations
        List<ParsedLogEntry> parsedEntries = new ArrayList<>(rawLogLines.size());
        for (String line : rawLogLines) {
            try {
                @SuppressWarnings("unchecked")
                Map<String, Object> map = mapper.readValue(line, Map.class);
                parsedEntries.add(new ParsedLogEntry(line, map));
            } catch (Exception e) {
                // Fallback for non-JSON lines
                Map<String, Object> fallbackMap = new HashMap<>();
                parsedEntries.add(new ParsedLogEntry(line, fallbackMap));
            }
        }

        // For each active subscription, filter the batch and send it
        for (Map.Entry<WebSocketSession, TailSubscription> entry : subscriptions.entrySet()) {
            WebSocketSession session = entry.getKey();
            TailSubscription sub = entry.getValue();

            if (!session.isOpen()) {
                subscriptions.remove(session);
                continue;
            }

            List<Map<String, Object>> matchedStreams = new ArrayList<>();
            List<Object[]> values = new ArrayList<>();

            for (ParsedLogEntry ple : parsedEntries) {
                if (sub.matches(ple.map, ple.rawLine)) {
                    // Collect metadata labels
                    Map<String, String> labels = new HashMap<>();
                    labels.put("job", (String) ple.map.getOrDefault("job", "app-logs"));
                    labels.put("level", (String) ple.map.getOrDefault("level", "INFO"));
                    labels.put("host", (String) ple.map.getOrDefault("host", "localhost"));

                    // Loki uses nanosecond timestamps as strings
                    long nsTimestamp = System.currentTimeMillis() * 1_000_000L;
                    Object tsObj = ple.map.get("timestamp");
                    if (tsObj instanceof Number) {
                        nsTimestamp = ((Number) tsObj).longValue() * 1_000_000L;
                    }

                    values.add(new Object[] { String.valueOf(nsTimestamp), ple.rawLine });
                }
            }

            if (!values.isEmpty()) {
                // Format matching streams in the standard Loki payload structure
                Map<String, Object> streamObj = new HashMap<>();
                Map<String, String> labels = new HashMap<>();
                labels.put("job", "aarchgate-live-tail");
                streamObj.put("stream", labels);
                streamObj.put("values", values);

                Map<String, Object> response = new HashMap<>();
                response.put("streams", Collections.singletonList(streamObj));

                try {
                    String jsonPayload = mapper.writeValueAsString(response);
                    session.sendMessage(new TextMessage(jsonPayload));
                } catch (IOException e) {
                    System.err.println("[WebSockets] Failed to send tail logs to session: " + session.getId() + " - " + e.getMessage());
                }
            }
        }
    }

    private static class ParsedLogEntry {
        final String rawLine;
        final Map<String, Object> map;

        ParsedLogEntry(String rawLine, Map<String, Object> map) {
            this.rawLine = rawLine;
            this.map = map;
        }
    }
}
