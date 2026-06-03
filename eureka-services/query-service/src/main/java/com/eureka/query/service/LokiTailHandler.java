package com.eureka.query.service;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import java.net.URI;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

@Component
public class LokiTailHandler extends TextWebSocketHandler {

    private final RealTimeLogBroadcaster broadcaster;

    @Autowired
    public LokiTailHandler(RealTimeLogBroadcaster broadcaster) {
        this.broadcaster = broadcaster;
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) throws Exception {
        URI uri = session.getUri();
        String query = "";

        if (uri != null && uri.getQuery() != null) {
            Map<String, String> params = parseQueryParams(uri.getQuery());
            if (params.containsKey("query")) {
                query = URLDecoder.decode(params.get("query"), StandardCharsets.UTF_8);
            }
        }

        broadcaster.registerSession(session, query);
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) throws Exception {
        broadcaster.deregisterSession(session);
    }

    private Map<String, String> parseQueryParams(String queryStr) {
        Map<String, String> map = new HashMap<>();
        String[] pairs = queryStr.split("&");
        for (String pair : pairs) {
            int idx = pair.indexOf("=");
            if (idx > 0) {
                String key = pair.substring(0, idx);
                String value = pair.substring(idx + 1);
                map.put(key, value);
            }
        }
        return map;
    }
}
