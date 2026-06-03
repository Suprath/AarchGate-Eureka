package com.eureka.query.service;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class LogQLParserTest {

    @Test
    public void testBasicSelectorOnly() {
        assertEquals("status == 500", LogQLParser.parse("{job=\"app\", status=\"500\"}"));
    }

    @Test
    public void testMultipleSelectors() {
        assertEquals("status == 500 AND latency > 200", LogQLParser.parse("{job=\"app\", status=\"500\", latency=\"200\"}"));
    }

    @Test
    public void testLineFilterOnly() {
        assertEquals("Database timeout", LogQLParser.parse("{job=\"app\"} |= \"Database timeout\""));
    }

    @Test
    public void testCombinedSelectorsAndFilters() {
        assertEquals("status == 500 AND latency > 150", LogQLParser.parse("{job=\"app\", status=\"500\", latency=\"150\"} |= \"error\""));
    }

    @Test
    public void testEmptyQuery() {
        assertEquals("", LogQLParser.parse(""));
        assertEquals("", LogQLParser.parse("{job=\"app\"}"));
    }
}
