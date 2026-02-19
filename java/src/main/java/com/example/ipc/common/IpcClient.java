package com.example.ipc.common;

public interface IpcClient {
    String send(String json) throws Exception;
}
