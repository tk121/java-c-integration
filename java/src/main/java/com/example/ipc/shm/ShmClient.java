package com.example.ipc.shm;

import com.example.ipc.common.IpcClient;

import java.io.RandomAccessFile;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;

public class ShmClient implements IpcClient {

    private static final String FILE = "/tmp/ipc_shm.dat";
    private static final int SIZE = 1024;

    @Override
    public String send(String json) throws Exception {

        RandomAccessFile raf = new RandomAccessFile(FILE, "rw");
        FileChannel channel = raf.getChannel();

        MappedByteBuffer buffer =
                channel.map(FileChannel.MapMode.READ_WRITE, 0, SIZE);

        buffer.put(json.getBytes());

        Thread.sleep(2000); // 簡易待機

        byte[] bytes = new byte[SIZE];
        buffer.position(0);
        buffer.get(bytes);

        return new String(bytes).trim();
    }
}
