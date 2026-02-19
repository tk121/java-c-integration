package com.example.ipc.jni;

public class NativeBridge {

    static {
        System.loadLibrary("nativeimpl"); // libnativeimpl.so
    }

    public native String process(String json);
}