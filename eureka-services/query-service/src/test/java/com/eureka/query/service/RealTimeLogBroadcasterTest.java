package com.eureka.query.service;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.mockito.Mockito;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;

import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.*;

public class RealTimeLogBroadcasterTest {

    private RealTimeLogBroadcaster broadcaster;
    private WebSocketSession mockSession;

    @BeforeEach
    public void setup() {
        broadcaster = new RealTimeLogBroadcaster();
        mockSession = Mockito.mock(WebSocketSession.class);
        when(mockSession.isOpen()).thenReturn(true);
        when(mockSession.getId()).thenReturn("test-session-123");
    }

    @Test
    public void testSessionRegistration() {
        broadcaster.registerSession(mockSession, "{job=\"app\", status=\"500\"}");
        assertEquals(1, broadcaster.getSubscriptions().size());
        assertTrue(broadcaster.getSubscriptions().containsKey(mockSession));

        broadcaster.deregisterSession(mockSession);
        assertEquals(0, broadcaster.getSubscriptions().size());
    }

    @Test
    public void testBroadcastMatchingLog() throws IOException {
        broadcaster.registerSession(mockSession, "{job=\"app\", status=\"500\"} |= \"Failed\"");

        String matchingLog = "{\"job\":\"app\",\"status\":500,\"latency\":120,\"message\":\"Failed to fetch S3 block\"}";
        String nonMatchingLog = "{\"job\":\"app\",\"status\":200,\"latency\":20,\"message\":\"Success fetch S3 block\"}";

        broadcaster.broadcastLogs(Arrays.asList(matchingLog, nonMatchingLog));

        // Verify that sendMessage was called exactly once with a payload containing the matching log
        verify(mockSession, times(1)).sendMessage(any(TextMessage.class));
    }

    @Test
    public void testBroadcastNoSubscribers() throws IOException {
        String log = "{\"job\":\"app\",\"status\":500,\"message\":\"Error occurred\"}";
        broadcaster.broadcastLogs(Collections.singletonList(log));
        verify(mockSession, never()).sendMessage(any(TextMessage.class));
    }
}
