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
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
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
 * only the player can grant, in the system's settings. This activity asks,
 * sends the player there, and starts the game either way: without the access
 * the game folder is the app's own, Android/data/<package>/files, which needs
 * none (src/paths.cpp decides that on the native side when no SNAP_DATA_DIR
 * reaches it), and a toast names it at every start, since a file manager may
 * not show it.
 *
 * Only an answer settles the question. A tap beside it leaves it up; Back
 * leaves the app, and the next start asks again; so does "Not now". "Don't
 * ask again" keeps the app's own folder. (1.1.6 marked the question asked
 * before it showed it, so a tap beside it on an AYN Odin 3 started the game
 * in the app's own folder and the question never came back.) The first start
 * in Snap64 after a start in the app's own folder copies that folder's files
 * across, where Snap64 has none of that name, so the saves come along.
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
    // "own" once the player has answered "Don't ask again". (1.1.6 kept
    // all_files_asked, set before the question showed, so it is not read:
    // a player it stranded is asked again.)
    private static final String PREF_ANSWER = "all_files_answer";
    private static final String PREF_ASKED_116 = "all_files_asked";
    // Set once the app's own folder's files were copied into Snap64, and
    // cleared by a start in the app's own folder.
    private static final String PREF_MOVED = "own_folder_copied";
    private static final int REQUEST_WRITE = 1;

    private boolean sentToSettings = false;
    private AlertDialog question;

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
            startInPublicFolder();
            return;
        }
        final SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        if ("own".equals(prefs.getString(PREF_ANSWER, null))) {
            // "Don't ask again": the app's own folder, until the player
            // grants the access in the system settings.
            startInOwnFolder();
            return;
        }
        prefs.edit().remove(PREF_ASKED_116).apply();
        question = new AlertDialog.Builder(this)
            .setTitle(getString(R.string.app_name))
            .setMessage("The game folder is Internal storage > Snap64: put your Pokemon Snap ROM, mods and texture packs there, and find your photos and the log there.\n\n"
                        + "Using it takes \"All files access\" for Snap64 Recomp. Without it, the game folder is the app's own: " + ownFolderName() + ".\n\n"
                        + "\"Not now\" asks again at the next start.")
            .setPositiveButton("Allow", (dialog, which) -> askForAccess())
            .setNegativeButton("Not now", (dialog, which) -> startInOwnFolder())
            .setNeutralButton("Don't ask again", (dialog, which) -> {
                prefs.edit().putString(PREF_ANSWER, "own").apply();
                startInOwnFolder();
            })
            // Back leaves the app unanswered; the next start asks again.
            .setOnCancelListener(dialog -> finish())
            .create();
        // A tap beside the question is no answer.
        question.setCanceledOnTouchOutside(false);
        question.show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (sentToSettings) {
            // Back from the system's settings page, with or without the
            // access; without it, the next start asks again.
            sentToSettings = false;
            if (hasAllFilesAccess()) {
                startInPublicFolder();
            } else {
                startInOwnFolder();
            }
        }
    }

    @Override
    protected void onDestroy() {
        // A question still up as the activity goes (a rotation makes it
        // again) is closed with it.
        if (question != null && question.isShowing()) {
            question.dismiss();
        }
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_WRITE) {
            if (hasAllFilesAccess()) {
                startInPublicFolder();
            } else {
                startInOwnFolder();
            }
        }
    }

    // The app's own folder as a file manager names it.
    private String ownFolderName() {
        return "Android/data/" + getPackageName() + "/files";
    }

    // The game in the app's own folder, which a toast names: the folder a
    // player looks for in a file manager is Snap64. (A toast shows two lines
    // at most, which the whole path overruns; the question gave it whole.)
    private void startInOwnFolder() {
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(PREF_MOVED, false).apply();
        Toast.makeText(getApplicationContext(), "Game folder: the app's own, in Android/data", Toast.LENGTH_LONG).show();
        startGame(null);
    }

    // The game in Snap64. The first time after a start in the app's own
    // folder (or ever, from 1.1.6, which kept no record), that folder's files
    // are copied across where Snap64 has none of that name (the saves, the
    // photos, the settings, the ROM the file picker copied), off the main
    // thread, and a toast says so.
    private void startInPublicFolder() {
        final File folder = publicFolder();
        final SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        final File own = getExternalFilesDir(null);
        final String[] held = (own != null && own.isDirectory()) ? own.list() : null;
        if (prefs.getBoolean(PREF_MOVED, false) || held == null || held.length == 0) {
            prefs.edit().putBoolean(PREF_MOVED, true).apply();
            startGame(folder);
            return;
        }
        new Thread(() -> {
            if (!folder.isDirectory() && !folder.mkdirs()) {
                Log.w(TAG, "the game folder " + folder + " could not be made");
            }
            final int copied;
            // One copy at a time: a launcher made again (a rotation) while
            // one runs finds its files there.
            synchronized (Snap64Launcher.class) {
                copied = copyMissing(own, folder);
            }
            Log.v(TAG, copied + " files copied from " + own + " to " + folder);
            runOnUiThread(() -> {
                prefs.edit().putBoolean(PREF_MOVED, true).apply();
                if (copied > 0) {
                    Toast.makeText(getApplicationContext(), "Your files were copied to Internal storage > Snap64",
                                   Toast.LENGTH_LONG).show();
                }
                startGame(folder);
            });
        }).start();
    }

    // Every file under from, into the same place under to, each only when
    // to does not have it yet; the log and the session mark stay behind.
    // The number of files copied.
    private int copyMissing(File from, File to) {
        final File[] entries = from.listFiles();
        if (entries == null) {
            return 0;
        }
        int copied = 0;
        for (File entry : entries) {
            final String name = entry.getName();
            final File target = new File(to, name);
            if (entry.isDirectory()) {
                if (!target.isDirectory() && !target.mkdirs()) {
                    Log.w(TAG, target + " could not be made");
                    continue;
                }
                copied += copyMissing(entry, target);
                continue;
            }
            if (target.exists() || name.equals("snap64.log") || name.equals("snap64.prev.log") || name.equals("snap64.running")
                || name.endsWith(".part")) {
                continue;
            }
            final File part = new File(to, name + ".part");
            try (InputStream in = new FileInputStream(entry);
                 OutputStream out = new FileOutputStream(part)) {
                final byte[] buf = new byte[65536];
                int n;
                while ((n = in.read(buf)) > 0) {
                    out.write(buf, 0, n);
                }
            } catch (Exception e) {
                Log.w(TAG, entry + " could not be copied to " + target + ": " + e);
                part.delete();
                continue;
            }
            if (part.renameTo(target)) {
                copied++;
            } else {
                Log.w(TAG, part + " could not be renamed to " + target);
                part.delete();
            }
        }
        return copied;
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
                startInOwnFolder();
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
