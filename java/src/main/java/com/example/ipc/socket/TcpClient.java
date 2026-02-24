package com.example.ipc.socket;

import com.example.ipc.common.IpcClient;

import java.io.*;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public class TcpClient implements IpcClient {

    private final String host;
    private final int port;

    public TcpClient(String host, int port) {
        this.host = host;
        this.port = port;
    }

    @Override
    public String send(String json) throws Exception {

        byte[] payload = json.getBytes(StandardCharsets.UTF_8);

        try (Socket socket = new Socket(host, port)) {
            socket.setSoTimeout(5000); // 必要に応じて調整

            DataOutputStream dos = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
            DataInputStream  dis = new DataInputStream(new BufferedInputStream(socket.getInputStream()));

            // ===== 送信: [4byte length][payload] =====
            dos.writeInt(payload.length); // 4byte big-endian
            dos.write(payload);
            dos.flush();

            // ===== 受信: [4byte length][payload] =====
            int respLen = dis.readInt(); // 4byte big-endian
            if (respLen < 0 || respLen > 10_000_000) { // 安全策（上限は適宜）
                throw new IOException("Invalid response length: " + respLen);
            }

            byte[] respBytes = new byte[respLen];
            dis.readFully(respBytes);

            return new String(respBytes, StandardCharsets.UTF_8);
        }
    }
}