package org.openra.android;

import android.os.Bundle;
import org.libsdl.app.SDLActivity;

public class OpenRAGameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "openal",
            "freetype6",
            "lua51",
            "launcher_jni"
        };
    }

    @Override
    protected String getMainSharedObject() {
        return "liblauncher_jni.so";
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        String engineDir = getIntent().getStringExtra("ENGINE_DIR");
        String supportDir = getIntent().getStringExtra("SUPPORT_DIR");
        String modId = getIntent().getStringExtra("MOD_ID");
        if (modId == null || modId.isEmpty()) {
            modId = "ra";
        }
        NativeBridge.nativeConfigureLaunch(0L, engineDir, supportDir, modId);
        super.onCreate(savedInstanceState);
    }
}
