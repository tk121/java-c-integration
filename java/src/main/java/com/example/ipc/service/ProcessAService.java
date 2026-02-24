package com.example.ipc.service;

import com.example.ipc.common.IpcClient;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

public class ProcessAService {

    private final IpcClient client;
    private final ObjectMapper mapper = new ObjectMapper();

    public ProcessAService(IpcClient client) {
        this.client = client;
    }

    public String execute(int a, int b) throws Exception {

        Map<String, Object> req = new HashMap<>();

        // C側ログで「どのTomcatスレッドか」を区別するため
        req.put("requestId", UUID.randomUUID().toString());
        req.put("threadId", Thread.currentThread().getId());

        // C側は processA/processB を見て処理分岐する想定
        req.put("command", "processA");

        Map<String, Object> data = new HashMap<>();
        data.put("a", a);
        data.put("b", b);
        req.put("data", data);

        String json = mapper.writeValueAsString(req);

        return client.send(json);
    }
}