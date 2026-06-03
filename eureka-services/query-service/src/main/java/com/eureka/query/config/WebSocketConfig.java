package com.eureka.query.config;

import com.eureka.query.service.LokiTailHandler;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {

    private final LokiTailHandler tailHandler;

    public WebSocketConfig(LokiTailHandler tailHandler) {
        this.tailHandler = tailHandler;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(tailHandler, "/loki/api/v1/tail")
                .setAllowedOrigins("*");
    }
}
