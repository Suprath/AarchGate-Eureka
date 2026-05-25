package com.eureka.query.config;

import com.eureka.aarchgate.grpc.AarchGateServiceGrpc;
import io.grpc.ManagedChannel;
import io.grpc.ManagedChannelBuilder;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class GrpcClientConfig {

    @Value("${aarchgate.grpc.host:127.0.0.1}")
    private String host;

    @Value("${aarchgate.grpc.port:50052}")
    private int port;

    @Bean
    public ManagedChannel managedChannel() {
        return ManagedChannelBuilder.forAddress(host, port)
                .usePlaintext()
                .build();
    }

    @Bean
    public AarchGateServiceGrpc.AarchGateServiceBlockingStub blockingStub(ManagedChannel channel) {
        return AarchGateServiceGrpc.newBlockingStub(channel);
    }

    @Bean
    public AarchGateServiceGrpc.AarchGateServiceStub asyncStub(ManagedChannel channel) {
        return AarchGateServiceGrpc.newStub(channel);
    }
}
