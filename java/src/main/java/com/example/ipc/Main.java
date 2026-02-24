package com.example.ipc;

import com.example.ipc.socket.TcpClient;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;

public class Main {

    // 共有してOK（スレッドセーフ）
    private static final ObjectMapper mapper = new ObjectMapper();

    static class Worker implements Runnable {
        private final int workerNo;
        private final TcpClient client;
        private final CountDownLatch startGate;
        private final CountDownLatch doneGate;
        private final int repeat;

        Worker(int workerNo, TcpClient client, CountDownLatch startGate, CountDownLatch doneGate, int repeat) {
            this.workerNo = workerNo;
            this.client = client;
            this.startGate = startGate;
            this.doneGate = doneGate;
            this.repeat = repeat;
        }

        @Override
        public void run() {
            try {
                // 全スレッド同時スタート
                startGate.await();

                for (int i = 0; i < repeat; i++) {
                    long t0 = System.currentTimeMillis();

                    // Tomcat相当：同一Servletが複数スレッドで呼ばれる想定
                    long threadId = Thread.currentThread().getId();
                    String requestId = UUID.randomUUID().toString();

                    // C側が期待する JSON（requestId, threadId, command）
                    Map<String, Object> req = new HashMap<>();
                    req.put("requestId", requestId);
                    req.put("threadId", threadId);
                    req.put("command", "processA");

                    Map<String, Object> data = new HashMap<>();
                    data.put("a", workerNo);
                    data.put("b", i);
                    req.put("data", data);

                    String json = mapper.writeValueAsString(req);

                    // 送信（4byte lengthヘッダ版 TcpClient の send）
                    String resp = client.send(json);

                    long t1 = System.currentTimeMillis();

                    System.out.printf(
                            "[Java] workerNo=%d javaThreadId=%d requestId=%s repeat=%d elapsed=%dms resp=%s%n",
                            workerNo, threadId, requestId, i, (t1 - t0), resp
                    );
                }

            } catch (Exception e) {
                System.out.printf("[Java] workerNo=%d ERROR=%s%n", workerNo, e.toString());
            } finally {
                doneGate.countDown();
            }
        }
    }

    /**
     * args:
     *   0: host (default 192.168.100.2)
     *   1: port (default 5000)
     *   2: threads (default 20)
     *   3: repeat per thread (default 1)
     *
     * example:
     *   java -cp ... com.example.ipc.Main 192.168.100.2 5000 50 2
     *   (50スレッド * 2回 = 100リクエスト)
     */
    public static void main(String[] args) throws Exception {

        String host = (args.length >= 1) ? args[0] : "192.168.100.2";
        int port    = (args.length >= 2) ? Integer.parseInt(args[1]) : 5000;
        int threads = (args.length >= 3) ? Integer.parseInt(args[2]) : 20;
        int repeat  = (args.length >= 4) ? Integer.parseInt(args[3]) : 1;

        System.out.printf("[Java] host=%s port=%d threads=%d repeat=%d%n", host, port, threads, repeat);

        CountDownLatch startGate = new CountDownLatch(1);
        CountDownLatch doneGate  = new CountDownLatch(threads);

        // 注意：TcpClientは host/port を持つだけなので共有してOK。
        // ただし send() 内で Socket を毎回 new している前提（あなたの実装はそうなっている）。
        TcpClient client = new TcpClient(host, port);

        for (int i = 0; i < threads; i++) {
            new Thread(new Worker(i + 1, client, startGate, doneGate, repeat)).start();
        }

        // 全スレッド準備完了を待ってから一斉発射
        Thread.sleep(300);
        System.out.println("[Java] GO!");
        startGate.countDown();

        doneGate.await();
        System.out.println("[Java] DONE");
    }
}