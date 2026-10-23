package io.github.jackandbeans.snap64recomp;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The first screen: settles where the game folder is, then starts the game.
 *
 * The game folder is Snap64 at the root of the shared storage (Internal
 * storage > Snap64 in a file manager, the Snap64 folder a PC shows over USB):
 * the one folder holding the ROM, the settings, the saves, mods/,
 * texture_packs/, the photos and snap64.log, as the port's own folder does on
 * a PC. Writing there takes all-files access on Android 11 and later, which
 * only the player can grant, in the system's settings. This activity asks
 * once, sends the player there, and starts the game either way: without the
 * access the game folder is the app's own, Android/data/<package>/files, which
 * needs none (src/paths.cpp decides that on the native side when no
 * SNAP_DATA_DIR reaches it).
 *
 * Everything the launching intent carries is handed on: its extras, which the
 * game activity turns into environment variables (adb drives the port's test
 * tapes that way, with the same SNAP_* names as on a PC), and a document
 * opened "with" the app, copied into the app's cache first because the
 * permission to read it ends with this activity.
 */
public class Snap64Launcher extends Activity {
    private static final String TAG = "Snap64";
    private static final String FOLDER_NAME = "Snap64";
    private static final String PREFS = "snap64";
    private static final String PREF_ASKED = "all_files_asked";
    private static final int REQUEST_WRITE = 1;

    private boolean sentToSettings = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final Intent launching = getIntent();
        if (launching != null && launching.hasExtra("SNAP_DATA_DIR")) {
            // adb named the folder outright (a test rig): no question, the
            // game starts with it, and the port says in its log whether the
            // folder can be used.
            startGame(null);
            return;
        }
        if (hasAllFilesAccess()) {
            startGame(publicFolder());
            return;
        }
        final SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        if (prefs.getBoolean(PREF_ASKED, false)) {
            // Asked before and not granted: the app's own folder, until the
            // player grants the access in the system settings.
            startGame(null);
            return;
        }
        prefs.edit().putBoolean(PREF_ASKED, true).apply();
        new AlertDialog.Builder(this)
            .setTitle(getString(R.string.app_name))
            .setMessage("The game folder is Internal storage > Snap64: put your Pokemon Snap ROM, mods and texture packs there, and find your photos and the log there.\n\n"
                        + "Using it takes \"All files access\" for Snap64 Recomp. Without it, the game folder is the app's own: Android/data/" + getPackageName() + "/files.")
            .setPositiveButton("Allow", (dialog, which) -> askForAccess())
            .setNegativeButton("Not now", (dialog, which) -> startGame(null))
            .setOnCancelListener(dialog -> startGame(null))
            .show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (sentToSettings) {
            // Back from the system's settings page, with or without the access.
            sentToSettings = false;
            startGame(hasAllFilesAccess() ? publicFolder() : null);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_WRITE) {
            startGame(hasAllFilesAccess() ? publicFolder() : null);
        }
    }

    private boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void askForAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                sentToSettings = true;
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                         Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                sentToSettings = false;
                Log.w(TAG, "the all-files-access settings page could not open: " + e);
                startGame(null);
            }
        } else {
            requestPermissions(new String[] { Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQUEST_WRITE);
        }
    }

    private File publicFolder() {
        return new File(Environment.getExternalStorageDirectory(), FOLDER_NAME);
    }

    // Starts the game with the folder (null: the app's own) and everything
    // the launching intent carried, then leaves.
    private void startGame(File folder) {
        final Intent launching = getIntent();
        final Intent game = new Intent(this, Snap64Activity.class);
        if (launching != null && launching.getExtras() != null) {
            game.putExtras(launching.getExtras());
        }
        if (folder != null) {
            if (!folder.isDirectory() && !folder.mkdirs()) {
                Log.w(TAG, "the game folder " + folder + " could not be made; the app's own folder is used");
            } else {
                game.putExtra("snap.folder", folder.getAbsolutePath());
            }
        }
        if (launching != null && launching.getData() != null) {
            final String copy = copyDocument(launching.getData());
            if (copy != null) {
                // The port drops it on its window at the first frame
                // (src/main.cpp, SNAP_OPEN_FILE), which installs a mod or a
                // texture pack as a drop does on a PC.
                game.putExtra("SNAP_OPEN_FILE", copy);
            }
        }
        startActivity(game);
        finish();
    }

    // The document behind a content: or file: URI, copied into the app's
    // cache under its own name; null when it could not be read.
    private String copyDocument(Uri uri) {
        String name = null;
        if ("content".equals(uri.getScheme())) {
            try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
                if (c != null && c.moveToFirst()) {
                    name = c.getString(0);
                }
            } catch (Exception e) {
                Log.w(TAG, "the document's name could not be read: " + e);
            }
        }
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment();
        }
        if (name == null || name.isEmpty()) {
            name = "opened";
        }
        name = new File(name).getName();
        final File dir = new File(getCacheDir(), "open");
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
        Log.v(TAG, "opened " + uri + " as " + dest);
        return dest.getAbsolutePath();
    }
}
