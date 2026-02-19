package com.example.ipc.jni;

import com.example.ipc.common.IpcClient;

public class JniClient implements IpcClient {

    private NativeBridge bridge = new NativeBridge();

    @Override
    public String send(String json) {
        return bridge.process(json);
    }
}
