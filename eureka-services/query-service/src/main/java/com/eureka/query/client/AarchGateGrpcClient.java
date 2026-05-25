package com.eureka.query.client;

import com.eureka.aarchgate.grpc.AarchGateServiceGrpc;
import com.eureka.aarchgate.grpc.QueryRequest;
import com.eureka.aarchgate.grpc.QueryResponse;
import com.eureka.aarchgate.grpc.IngestRequest;
import com.eureka.aarchgate.grpc.IngestResponse;
import io.grpc.stub.StreamObserver;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

@Component
public class AarchGateGrpcClient {

    private final AarchGateServiceGrpc.AarchGateServiceBlockingStub blockingStub;
    private final AarchGateServiceGrpc.AarchGateServiceStub asyncStub;

    @Autowired
    public AarchGateGrpcClient(AarchGateServiceGrpc.AarchGateServiceBlockingStub blockingStub,
                               AarchGateServiceGrpc.AarchGateServiceStub asyncStub) {
        this.blockingStub = blockingStub;
        this.asyncStub = asyncStub;
    }

    public QueryResponse executeQuery(String queryString, String filePath, boolean pinMemory) {
        QueryRequest request = QueryRequest.newBuilder()
                .setQueryString(queryString)
                .setFilePath(filePath)
                .setPinMemory(pinMemory)
                .build();
        return blockingStub.executeQuery(request);
    }

    public IngestResponse streamIngest(String databasePath, List<String> logLines) throws InterruptedException {
        final IngestResponse[] result = new IngestResponse[1];
        final CountDownLatch latch = new CountDownLatch(1);

        StreamObserver<IngestResponse> responseObserver = new StreamObserver<IngestResponse>() {
            @Override
            public void onNext(IngestResponse value) {
                result[0] = value;
            }

            @Override
            public void onError(Throwable t) {
                latch.countDown();
            }

            @Override
            public void onCompleted() {
                latch.countDown();
            }
        };

        StreamObserver<IngestRequest> requestObserver = asyncStub.streamIngest(responseObserver);

        // Partition lines into batches of 1000
        int batchSize = 1000;
        for (int i = 0; i < logLines.size(); i += batchSize) {
            List<String> batch = logLines.subList(i, Math.min(i + batchSize, logLines.size()));
            IngestRequest request = IngestRequest.newBuilder()
                    .setDatabasePath(databasePath)
                    .addAllLogLines(batch)
                    .build();
            requestObserver.onNext(request);
        }
        requestObserver.onCompleted();

        latch.await(30, TimeUnit.SECONDS);
        return result[0];
    }
}
