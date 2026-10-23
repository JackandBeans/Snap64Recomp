package io.github.jackandbeans.snap64recomp;

import android.content.Intent;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.CountDownLatch;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity, which loads libSDL2.so and then libmain.so (the
 * port, built as a library for Android; BUILDING.md, step 16) and runs its
 * SDL_main on its own thread, with the port's four additions:
 *
 * - the game folder and the launching intent's extras reach the native side
 *   as environment variables before it starts (SNAP_DATA_DIR, and whatever
 *   adb passed: SNAP_REPLAY, SNAP_STATS, SNAP_MUTE and the rest the port
 *   reads with getenv on every platform);
 * - the files the port ships beside its executable on a PC (menu_text/, the
 *   seen-shader list, gamecontrollerdb.txt, the two README files) are
 *   copied from the package's assets into the game folder when missing;
 * - the system's document picker, which the native ROM chooser and the Mods
 *   page's Install row call through JNI (pickDocument), since no file dialog
 *   library serves Android;
 * - the touch controls over the picture (Snap64TouchOverlay).
 */
public class Snap64Activity extends SDLActivity {
    private static final String TAG = "Snap64";
    private static final int REQUEST_PICK = 1;

    private CountDownLatch pickLatch;
    private volatile String pickedPath = "";
    private Snap64TouchOverlay touch;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (SDLActivity.mBrokenLibraries) {
            return;
        }
        final Intent intent = getIntent();
        final Bundle extras = (intent != null) ? intent.getExtras() : null;

        // The game folder: the one adb named (SNAP_DATA_DIR, the port's own
        // switch on every platform), else the one the launcher settled, else
        // the app's own folder, which the native side takes on its own.
        String folder = (extras != null) ? extras.getString("SNAP_DATA_DIR") : null;
        if (folder == null && extras != null) {
            folder = extras.getString("snap.folder");
        }
        if (folder != null) {
            SDLActivity.nativeSetenv("SNAP_DATA_DIR", folder);
        }
        if (folder == null) {
            final File own = getExternalFilesDir(null);
            folder = (own != null) ? own.getAbsolutePath() : null;
        }

        // Every other extra is an environment variable for the native side.
        if (extras != null) {
            for (String key : extras.keySet()) {
                if (key.startsWith("snap.") || key.equals("SNAP_DATA_DIR")) {
                    continue;
                }
                final Object value = extras.get(key);
                if (value != null) {
                    SDLActivity.nativeSetenv(key, value.toString());
                }
            }
        }

        // The shipped files, where the native side reads them from
        // (src/paths.cpp: on Android the shipped files' folder is the game
        // folder itself). They are the assets' snap64/ folder: the root of
        // the assets lists the system's own folders as well (images, webkit,
        // geoid_map), which went into the game folder with them.
        if (folder != null) {
            copyAssetsIfMissing(getAssets(), "snap64", new File(folder));
        }

        // The game's buttons over the picture, for a phone with no pad.
        if (SDLActivity.mLayout != null) {
            touch = new Snap64TouchOverlay(this, SDLActivity.mLayout);
        }
    }

    @Override
    protected void onPause() {
        // A finger on a button as the app leaves may never see its lift.
        if (touch != null) {
            touch.release();
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (touch != null) {
            touch.stop();
            touch = null;
        }
        super.onDestroy();
    }

    // "Open with" while the game is running: the launcher copied the document
    // and comes here (this activity is a single instance); the port installs
    // it as a drop on its window.
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        final String opened = (intent != null) ? intent.getStringExtra("SNAP_OPEN_FILE") : null;
        if (opened != null && !SDLActivity.mBrokenLibraries) {
            Log.v(TAG, "dropping " + opened);
            SDLActivity.onNativeDropFile(opened);
        }
    }

    /**
     * Called from the native side (src/android.cpp) on the SDL thread: shows
     * the system's document picker and waits for the choice. Returns the path
     * of a copy of the chosen document in the app's cache, which the caller
     * removes when done, or an empty string when the player chose nothing.
     */
    public String pickDocument() {
        pickLatch = new CountDownLatch(1);
        pickedPath = "";
        runOnUiThread(() -> {
            try {
                final Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                pick.addCategory(Intent.CATEGORY_OPENABLE);
                pick.setType("*/*");
                startActivityForResult(pick, REQUEST_PICK);
            } catch (Exception e) {
                Log.w(TAG, "the document picker could not open: " + e);
                pickLatch.countDown();
            }
        });
        try {
            pickLatch.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        return pickedPath;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_PICK) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                final String copy = copyDocument(data.getData(), new File(getCacheDir(), "picked"));
                pickedPath = (copy != null) ? copy : "";
            }
            if (pickLatch != null) {
                pickLatch.countDown();
            }
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    // The document behind a content: URI, copied under its own name into
    // dir; null when it could not be read.
    private String copyDocument(Uri uri, File dir) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                name = c.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "the document's name could not be read: " + e);
        }
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment();
        }
        if (name == null || name.isEmpty()) {
            name = "picked";
        }
        name = new File(name).getName();
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return null;
        }
        final File dest = new File(dir, name);
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(dest)) {
            if (in == null) {
                return null;
            }
            final byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
        } catch (Exception e) {
            Log.w(TAG, "the document " + uri + " could not be copied: " + e);
            dest.delete();
            return null;
        }
        return dest.getAbsolutePath();
    }

    // The package's assets under path, into dest, each file only when dest
    // does not have it yet: a player's own copy, or one the port appended
    // to (the seen-shader list), is never overwritten.
    private void copyAssetsIfMissing(AssetManager assets, String path, File dest) {
        String[] entries;
        try {
            entries = assets.list(path);
        } catch (Exception e) {
            Log.w(TAG, "the assets under '" + path + "' could not be listed: " + e);
            return;
        }
        if (entries == null) {
            return;
        }
        for (String entry : entries) {
            final String child = path.isEmpty() ? entry : path + "/" + entry;
            String[] below = null;
            try {
                below = assets.list(child);
            } catch (Exception ignored) {
            }
            final File target = new File(dest, entry);
            if (below != null && below.length > 0) {
                copyAssetsIfMissing(assets, child, target);
                continue;
            }
            if (target.exists()) {
                continue;
            }
            final File parent = target.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                Log.w(TAG, parent + " could not be made");
                continue;
            }
            final File part = new File(parent, entry + ".part");
            try (InputStream in = assets.open(child);
                 OutputStream out = new FileOutputStream(part)) {
                final byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            } catch (Exception e) {
                Log.w(TAG, child + " could not be copied to " + target + ": " + e);
                part.delete();
                continue;
            }
            if (!part.renameTo(target)) {
                Log.w(TAG, part + " could not be renamed to " + target);
                part.delete();
            }
        }
    }
}
