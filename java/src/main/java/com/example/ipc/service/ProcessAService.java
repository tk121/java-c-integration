package com.example.ipc.service;

import com.example.ipc.common.IpcClient;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.HashMap;
import java.util.Map;

public class ProcessAService {

    private IpcClient client;
    private ObjectMapper mapper = new ObjectMapper();

    public ProcessAService(IpcClient client) {
        this.client = client;
    }

    public String execute(int a, int b) throws Exception {

        Map<String, Object> req = new HashMap<>();
        req.put("command", "processA");

        Map<String, Object> data = new HashMap<>();
        data.put("a", a);
        data.put("b", b);

        req.put("data", data);

        String json = mapper.writeValueAsString(req);

        return client.send(json);
    }
}
