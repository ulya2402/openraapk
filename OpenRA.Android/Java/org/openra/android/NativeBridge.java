package org.openra.android;

public final class NativeBridge {
    static {
        System.loadLibrary("launcher_jni");
    }

    public interface ProgressCallback {
        void onProgress(int percent, String message);
    }

    public static native String nativeExtractAndVerify(
        int fd,
        String destDir,
        ProgressCallback callback
    );

    public static native String nativeGetAbiInfo();

    public static native void nativeConfigureLaunch(
        long entryFnPtr,
        String engineDir,
        String supportDir,
        String modId
    );
}
