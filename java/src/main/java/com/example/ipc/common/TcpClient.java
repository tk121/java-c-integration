package com.example.ipc.common;

import java.io.*;
import java.net.Socket;

public class TcpClient implements IpcClient {

    private String host;
    private int port;

    public TcpClient(String host, int port) {
        this.host = host;
        this.port = port;
    }

    @Override
    public String send(String json) throws Exception {
        try (Socket socket = new Socket(host, port);
             BufferedReader reader = new BufferedReader(
                     new InputStreamReader(socket.getInputStream()));
             PrintWriter writer = new PrintWriter(
                     socket.getOutputStream(), true)) {

            writer.println(json);      // 送信
            return reader.readLine();  // 受信
        }
    }
}
