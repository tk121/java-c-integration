package com.example.ipc;

import com.example.ipc.common.TcpClient;
import com.example.ipc.jni.JniClient;
import com.example.ipc.service.ProcessAService;

public class Main {
    public static void main(String[] args) throws Exception {

        // TcpClient client = new TcpClient("192.168.100.2", 5000);
        JniClient client = new JniClient(); 
        ProcessAService service = new ProcessAService(client);

        String response = service.execute(10, 20);

        System.out.println("Response: " + response);
    }
}
