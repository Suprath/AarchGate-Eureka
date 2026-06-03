package com.eureka.query.controller;

import com.eureka.aarchgate.grpc.IngestResponse;
import com.eureka.aarchgate.grpc.QueryResponse;
import com.eureka.query.service.QueryExecutionService;
import com.eureka.query.service.RealTimeLogBroadcaster;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.web.servlet.WebMvcTest;
import org.springframework.boot.test.mock.mockito.MockBean;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;

import java.util.Collections;
import java.util.HashMap;

import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import com.eureka.query.service.MockIngestionSimulator;

@WebMvcTest(LokiController.class)
public class LokiControllerTest {

    @Autowired
    private MockMvc mockMvc;

    @MockBean
    private QueryExecutionService queryExecutionService;

    @MockBean
    private RealTimeLogBroadcaster broadcaster;

    @MockBean
    private MockIngestionSimulator simulator;

    @BeforeEach
    public void setup() {
        QueryResponse queryResponse = QueryResponse.newBuilder()
                .setTotalMatches(10)
                .setBytesProcessed(1024)
                .setExecutionTimeMs(5.0)
                .setSpeedGbSec(0.2)
                .build();
        when(queryExecutionService.executeLogQuery(anyString(), anyString(), anyBoolean()))
                .thenReturn(queryResponse);

        IngestResponse ingestResponse = IngestResponse.newBuilder()
                .setSuccess(true)
                .setTotalIngested(5)
                .build();
        when(queryExecutionService.ingestLogLines(anyString(), anyList()))
                .thenReturn(ingestResponse);
    }

    @Test
    public void testReadyEndpoint() throws Exception {
        mockMvc.perform(get("/ready"))
                .andExpect(status().isOk());
    }

    @Test
    public void testLabelsEndpoint() throws Exception {
        mockMvc.perform(get("/loki/api/v1/labels"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("success"))
                .andExpect(jsonPath("$.data[0]").value("job"));
    }

    @Test
    public void testLabelValuesEndpoint() throws Exception {
        mockMvc.perform(get("/loki/api/v1/label/job/values"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("success"))
                .andExpect(jsonPath("$.data[0]").value("app-logs"));
    }

    @Test
    public void testPushLogsEndpoint() throws Exception {
        String lokiPushPayload = "{\n" +
                "  \"streams\": [\n" +
                "    {\n" +
                "      \"stream\": {\n" +
                "        \"job\": \"app-logs\"\n" +
                "      },\n" +
                "      \"values\": [\n" +
                "        [ \"1569274316000000000\", \"{\\\"status\\\":200,\\\"message\\\":\\\"OK\\\"}\" ]\n" +
                "      ]\n" +
                "    }\n" +
                "  ]\n" +
                "}";

        mockMvc.perform(post("/loki/api/v1/push")
                .contentType(MediaType.APPLICATION_JSON)
                .content(lokiPushPayload))
                .andExpect(status().isNoContent());
    }

    @Test
    public void testQueryRangeEndpoint() throws Exception {
        mockMvc.perform(get("/loki/api/v1/query_range")
                .param("query", "{job=\"app-logs\", status=\"200\"}")
                .param("limit", "10"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.status").value("success"))
                .andExpect(jsonPath("$.data.resultType").value("streams"));
    }
}
