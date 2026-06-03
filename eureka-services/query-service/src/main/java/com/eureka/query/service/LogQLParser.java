package com.eureka.query.service;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class LogQLParser {

    private static final Pattern STATUS_PATTERN = Pattern.compile("status\\s*(=~|!=|==|=)\\s*\"?(\\d+)\"?");
    private static final Pattern LATENCY_PATTERN = Pattern.compile("latency\\s*(>=|<=|>|<|==|=)\\s*\"?(\\d+)\"?");
    private static final Pattern LINE_FILTER_PATTERN = Pattern.compile("\\|=\\s*\"([^\"]*)\"");

    /**
     * Translates a standard Grafana LogQL query into the query string expected by AarchGate.
     * e.g., `{job="app-logs", status="500"} |= "Database timeout"` -> `status == 500`
     * or for text search if no status/latency: `"Database timeout"`
     */
    public static String parse(String logql) {
        if (logql == null || logql.trim().isEmpty()) {
            return "";
        }

        Integer statusVal = null;
        Integer latencyVal = null;
        String lineFilterText = null;

        // 1. Extract status label from stream selector
        Matcher statusMatcher = STATUS_PATTERN.matcher(logql);
        if (statusMatcher.find()) {
            try {
                statusVal = Integer.parseInt(statusMatcher.group(2));
            } catch (NumberFormatException ignored) {}
        }

        // 2. Extract latency label from stream selector
        Matcher latencyMatcher = LATENCY_PATTERN.matcher(logql);
        if (latencyMatcher.find()) {
            try {
                latencyVal = Integer.parseInt(latencyMatcher.group(2));
            } catch (NumberFormatException ignored) {}
        }

        // 3. Extract string line filters
        Matcher lineFilterMatcher = LINE_FILTER_PATTERN.matcher(logql);
        if (lineFilterMatcher.find()) {
            lineFilterText = lineFilterMatcher.group(1);
        }

        // 4. Synthesize the output query string
        StringBuilder queryBuilder = new StringBuilder();
        if (statusVal != null) {
            queryBuilder.append("status == ").append(statusVal);
        }

        if (latencyVal != null) {
            if (queryBuilder.length() > 0) {
                queryBuilder.append(" AND ");
            }
            queryBuilder.append("latency > ").append(latencyVal);
        }

        if (queryBuilder.length() > 0) {
            return queryBuilder.toString();
        }

        // Fallback: If no status/latency filters, return the extracted line filter text for Scalar String Search
        if (lineFilterText != null) {
            return lineFilterText;
        }

        // If it's a completely empty stream selector or unrecognized, return default
        return "";
    }
}
