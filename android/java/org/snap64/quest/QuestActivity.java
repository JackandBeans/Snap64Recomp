package org.snap64.quest;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import java.io.*;

public class QuestActivity extends SDLActivity {
    private RomImportController romImport;
    public int romImportState() { return romImport.state; }
    public int romImportProgress() { return romImport.progress; }
    public String romImportMessage() { return romImport.message; }
    public void chooseRom() { romImport.choose(); }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == RomImportController.PICK_ROM) romImport.result(result, data);
    }
    @Override protected void onDestroy() {
        if (romImport != null) romImport.stop();
        super.onDestroy();
    }
    private static native void nativeSetDataDirectory(String path);
    private static native void nativeSetSurface(Surface surface);
    @Override protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context) {
            // OpenXR supplies the headset images. This surface is only RT64's
            // companion output; keep Android from allocating a 4128x2208 copy.
            { getHolder().setFixedSize(640, 480); }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                nativeSetSurface(holder.getSurface());
                super.surfaceChanged(holder, format, width, height);
            }
            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                nativeSetSurface(null);
                super.surfaceDestroyed(holder);
            }
        };
    }
    @Override protected String[] getLibraries() { return new String[]{"SDL2", "openxr_loader", "Snap64RecompVR"}; }
    @Override protected void onCreate(Bundle state) {
        // SDL starts its native thread after the surface is ready. Prepare all
        // ordinary filesystem assets first; saves and the ROM survive updates.
        File directory = getExternalFilesDir(null);
        if (directory == null) directory = getFilesDir();
        try { copyAssets("", directory); }
        catch (IOException e) { throw new RuntimeException("Unable to install game assets", e); }
        if (getPackageName().endsWith(".benchmark")) {
            new File(directory, "saves").mkdirs();
            new File(directory, "benchmark/results").mkdirs();
        }
        System.loadLibrary("SDL2");
        System.loadLibrary("openxr_loader");
        System.loadLibrary("Snap64RecompVR");
        nativeSetDataDirectory(directory.getAbsolutePath());
        romImport = new RomImportController(this, directory);
        super.onCreate(state);
    }
    private void copyAssets(String path, File destination) throws IOException {
        String[] children = getAssets().list(path);
        if (children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) throw new IOException(destination.toString());
            for (String child : children) copyAssets(path.isEmpty() ? child : path + "/" + child, new File(destination, child));
        } else {
            // The seen-shader cache is seeded once. Runtime additions survive
            // launches and APK updates, just like settings and save data.
            if (path.startsWith("cache/") && destination.isFile()) return;
            try (InputStream in = getAssets().open(path); OutputStream out = new FileOutputStream(destination)) {
                byte[] buffer = new byte[65536]; int size;
                while ((size = in.read(buffer)) != -1) out.write(buffer, 0, size);
            }
        }
    }
}
