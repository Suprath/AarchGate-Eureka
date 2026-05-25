package com.eureka.query.service;

import com.eureka.aarchgate.grpc.QueryResponse;
import com.eureka.aarchgate.grpc.IngestResponse;
import com.eureka.query.client.AarchGateGrpcClient;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.util.List;

@Service
public class QueryExecutionService {

    private final AarchGateGrpcClient grpcClient;
    private final ScratchStorageService scratchStorageService;

    @Autowired
    public QueryExecutionService(AarchGateGrpcClient grpcClient, ScratchStorageService scratchStorageService) {
        this.grpcClient = grpcClient;
        this.scratchStorageService = scratchStorageService;
    }

    public QueryResponse executeLogQuery(String query, String filePath, boolean pinMemory) {
        try {
            String resolvedPath = scratchStorageService.resolveAndFetch(filePath);
            return grpcClient.executeQuery(query, resolvedPath, pinMemory);
        } catch (IOException e) {
            return QueryResponse.newBuilder()
                    .setErrorMessage("Cache staging failed: " + e.getMessage())
                    .build();
        }
    }

    public IngestResponse ingestLogLines(String databasePath, List<String> logLines) {
        try {
            String resolvedPath = scratchStorageService.resolveAndFetch(databasePath);
            return grpcClient.streamIngest(resolvedPath, logLines);
        } catch (Exception e) {
            return IngestResponse.newBuilder()
                    .setSuccess(false)
                    .setErrorMessage("Ingestion target staging failed: " + e.getMessage())
                    .build();
        }
    }
}
